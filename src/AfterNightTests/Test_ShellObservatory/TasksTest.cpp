// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Tasks in the Observatory window, as a person meets them: every process opened is a task in the rail - its target's
/// thumbnail, its process's glyph, a page of the centre or a window of its own - which a target change, a run's
/// progress and a result seen elsewhere move; its menu; the notice over an image a run writes and the Show it offers,
/// while another view goes on as usual; the glyph on the tab of a task's image; and the pending step of the image's
/// history.

#include "ObservatoryWindowCase.h"

#include "pages/WorkspacePage.h"
#include "tasks/TaskPresenter.h"
#include "workspaces/WorkspaceHost.h"

#include "actions/ActionCatalogue.h"
#include "components/AnHistoryStrip.h"
#include "components/AnIconRail.h"
#include "components/RailItem.h"
#include "controls/ImageProcessControl.h"
#include "controls/ImageRTPreviewControl.h"
#include "controls/ImageViewControl.h"
#include "controls/KDDockWidgetsHelper.h"
#include "controls/ProcessPresentation.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "controls/ViewChrome.h"
#include "controls/ViewProcessingNotice.h"
#include "core/ApplicationCore.h"
#include "services/ProcessLaunchService.h"
#include "tasks/TaskModel.h"
#include "views/ViewChromeBinder.h"
#include "views/ViewThumbnailService.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPointer>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::observatory
{
namespace
{

using Window = an::ui::observatory::ObservatoryWindow;
using an::ui::ProcessRunState;
using an::ui::shell::Task;
using an::ui::shell::TaskId;
using Phase = ProcessRunState::Phase;

/// A process launched on an image, a page over a view of it.
constexpr std::string_view kTargetProcess = "denoising.actionDeepSNR";

/// A process of the integration workflow, launched on no image: it opens as a window of its own.
constexpr std::string_view kWindowProcess = "multi_image_stacking.actionImage_Stacking";
constexpr std::string_view kBrightnessContrast = "tone_brightness.actionAdjustBrightness_Contrast";

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
    const std::vector<an::ui::RailItem>& items = rail.items();
    const auto found =
        std::find_if(items.begin(), items.end(), [&id](const an::ui::RailItem& item) { return item.id == id; });
    return found != items.end() ? *found : an::ui::RailItem{};
}

/// The rail's task items.
[[nodiscard]] std::vector<an::ui::RailItem> taskItems(const an::ui::AnIconRail& rail)
{
    std::vector<an::ui::RailItem> tasks;
    for (const an::ui::RailItem& item : rail.items())
    {
        if (item.section == an::ui::RailSection::Task)
        {
            tasks.push_back(item);
        }
    }
    return tasks;
}

/// The entry of a menu by its text; null for none.
[[nodiscard]] QAction* entryOf(const QMenu& menu, const QString& text)
{
    for (QAction* action : menu.actions())
    {
        if (action->text() == text)
        {
            return action;
        }
    }
    return nullptr;
}

/// The cases of the tasks: each opens its processes over views of its own, as a person does.
class TaskCase : public ObservatoryWindowCase
{
protected:
    [[nodiscard]] static an::ui::observatory::TaskPresenter& tasks()
    {
        an::ui::observatory::TaskPresenter* presenter = window().taskPresenter();
        EXPECT_NE(presenter, nullptr);
        return *presenter;
    }

    /// Opens a process over the active view as its menu entry does, and answers the process it opened: a window of its
    /// own, or a page of the centre.
    static TopLevelProcessBaseControl* launch(std::string_view descriptorId)
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

    /// Makes the main window the active one again, where the platform activates windows at all.
    static void activateMainWindow()
    {
        if (s_platformActivatesWindows)
        {
            window().activateWindow();
            (void)QTest::qWaitForWindowActive(&window());
        }
        settle();
    }

    /// The task of a process window; fails the case when the model holds none.
    [[nodiscard]] static const Task* taskOf(const TopLevelProcessBaseControl* process)
    {
        const Task* task = tasks().model().taskFor(process);
        EXPECT_NE(task, nullptr) << "the process is no task";
        return task;
    }

    /// The notice a view shows now; null while it shows none.
    [[nodiscard]] static const an::ui::ViewProcessingNotice* noticeOf(const ImageViewControl& view)
    {
        for (const an::ui::ViewProcessingNotice* notice : view.findChildren<an::ui::ViewProcessingNotice*>())
        {
            if (notice->isVisibleTo(&view))
            {
                return notice;
            }
        }
        return nullptr;
    }

    /// The history strip of the workspace on screen.
    [[nodiscard]] static an::ui::AnHistoryStrip* strip()
    {
        an::ui::observatory::WorkspacePage* page = window().workspacePage();
        return page != nullptr ? page->historyStrip() : nullptr;
    }
};

class Tasks : public TaskCase
{
};

class BlockedView : public TaskCase
{
};

class TabGlyph : public TaskCase
{
};

class HistoryStrip : public TaskCase
{
};

} // namespace

// A process opened is a task in the rail: a page of the centre, named by its process and its image, with its glyph and
// its image's thumbnail; a process of the integration workflow in a window of its own, floating, its glyph on a tile;
// a process launched on no image named alone, its glyph on a tile. Each goes with its process.
TEST_F(Tasks, TheRailGainsAndLosesItems)
{
    ImageViewControl* const view = openView(QStringLiteral("tasks-gain"));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(taskItems(*window().rail()).empty());
    EXPECT_FALSE(window().rail()->hairlineRect().isValid()) << "no hairline without tasks";

    TopLevelProcessBaseControl* const process = launch(kTargetProcess);
    ASSERT_NE(process, nullptr);
    EXPECT_TRUE(process->isPresentedAsPage()) << "a process of a page is a page of the centre";
    const Task* task = taskOf(process);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->descriptorId, kTargetProcess);
    const std::vector<an::ui::RailItem> items = taskItems(*window().rail());
    ASSERT_EQ(items.size(), 1u);
    const an::ui::RailItem& item = items.front();
    EXPECT_EQ(item.id, Window::railTaskId(task->id));
    EXPECT_EQ(item.text, QStringLiteral("DeepSNR - tasks-gain"));
    EXPECT_EQ(item.glyphKey, task->iconKey);
    EXPECT_FALSE(item.floating) << "a task in a page does not float";
    EXPECT_EQ(item.runPhase, an::ui::RailRunPhase::None);
    EXPECT_TRUE(window().rail()->hairlineRect().isValid());
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() {
        return !railItem(*window().rail(), Window::railTaskId(task->id)).thumbnail.isNull();
    })) << "the image's thumbnail is shown";
    EXPECT_EQ(railItem(*window().rail(), Window::railTaskId(task->id)).thumbnail.size(),
              QSize(an::ui::shell::ViewThumbnailService::kSide, an::ui::shell::ViewThumbnailService::kSide));

    // A process with no single target is a task too, its glyph on a tile.
    TopLevelProcessBaseControl* const composition = launch("color.actionChannelComposition");
    ASSERT_NE(composition, nullptr);
    ASSERT_EQ(taskItems(*window().rail()).size(), 2u);
    EXPECT_TRUE(taskItems(*window().rail())[1].thumbnail.isNull());
    EXPECT_EQ(taskItems(*window().rail())[1].text, QStringLiteral("Channel Composition"));

    // A window of the integration workflow floats.
    TopLevelProcessBaseControl* const stacking = launch(kWindowProcess);
    ASSERT_NE(stacking, nullptr);
    EXPECT_TRUE(stacking->isWindow()) << "a process of no page is a window of its own";
    ASSERT_EQ(taskItems(*window().rail()).size(), 3u);
    EXPECT_TRUE(taskItems(*window().rail())[2].floating) << "a task in a window floats";
    EXPECT_TRUE(taskItems(*window().rail())[2].thumbnail.isNull());

    const QPointer<TopLevelProcessBaseControl> closing(process);
    process->close();
    settle();
    ASSERT_TRUE(closing.isNull());
    ASSERT_EQ(taskItems(*window().rail()).size(), 2u);
    EXPECT_EQ(taskItems(*window().rail())[0].text, QStringLiteral("Channel Composition"));
    stacking->close();
    settle();
}

// A process pointed at another image moves its task there: the thumbnail and the tooltip name the new image.
TEST_F(Tasks, ATargetChangeUpdatesTheThumbnailAndTheTooltip)
{
    ImageViewControl* const first = openView(QStringLiteral("tasks-first"), 0.1f);
    ImageViewControl* const second = openView(QStringLiteral("tasks-second"), 0.9f);
    ASSERT_TRUE(first != nullptr && second != nullptr);
    core().viewService().setActiveImageViewControl(first);
    auto* const process = dynamic_cast<ImageProcessControl*>(launch(kTargetProcess));
    ASSERT_NE(process, nullptr);
    const Task* task = taskOf(process);
    ASSERT_NE(task, nullptr);
    const QString id = Window::railTaskId(task->id);
    ASSERT_TRUE(
        an::tests::app::processEventsUntil([&]() { return !railItem(*window().rail(), id).thumbnail.isNull(); }));
    const qint64 before = railItem(*window().rail(), id).thumbnail.cacheKey();
    EXPECT_TRUE(window().rail()->itemToolTip(id).contains(QStringLiteral("tasks-first")));

    process->changeTargetViewControl(second);
    settle();
    EXPECT_EQ(railItem(*window().rail(), id).text, QStringLiteral("DeepSNR - tasks-second"));
    EXPECT_TRUE(window().rail()->itemToolTip(id).contains(QStringLiteral("tasks-second")));
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() {
        const QImage shown = railItem(*window().rail(), id).thumbnail;
        return !shown.isNull() && shown.cacheKey() != before;
    })) << "the thumbnail is the new image's";
    EXPECT_EQ(railItem(*window().rail(), id).thumbnail.cacheKey(), tasks().thumbnails().thumbnail(second).cacheKey());
}

// The ring's arc follows a run's progress, and the tooltip says it.
TEST_F(Tasks, TheArcFollowsTheProgress)
{
    ImageViewControl* const view = openView(QStringLiteral("tasks-arc"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kTargetProcess);
    ASSERT_NE(process, nullptr);
    const QString id = Window::railTaskId(taskOf(process)->id);

    process->setRunState(an::ui::runningState({}, 0.42));
    EXPECT_EQ(railItem(*window().rail(), id).runPhase, an::ui::RailRunPhase::Running);
    EXPECT_EQ(railItem(*window().rail(), id).progress, std::optional<int>(42));
    EXPECT_TRUE(window().rail()->itemToolTip(id).contains(QStringLiteral("applying 42 %")));
    process->reportRunProgress(0.6);
    EXPECT_EQ(railItem(*window().rail(), id).progress, std::optional<int>(60));
    process->setRunState(ProcessRunState{.phase = Phase::Cancelled});
    EXPECT_EQ(railItem(*window().rail(), id).runPhase, an::ui::RailRunPhase::None) << "a stop shows nothing";
}

// A run that ended while another place was shown leaves its result's dot - succeeded or failed - which Show clears.
TEST_F(Tasks, TheResultDotOfARunThatEndedElsewhereClearsOnShow)
{
    ImageViewControl* const view = openView(QStringLiteral("tasks-dot"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kTargetProcess);
    ASSERT_NE(process, nullptr);
    const Task* task = taskOf(process);
    ASSERT_NE(task, nullptr);
    const TaskId taskId = task->id;
    const QString id = Window::railTaskId(taskId);
    activateMainWindow();
    if (tasks().model().current() == taskId)
    {
        (void)tasks().model().setCurrent(0u);
    }

    process->setRunState(an::ui::runningState());
    process->setRunState(ProcessRunState{.phase = Phase::Succeeded});
    EXPECT_TRUE(railItem(*window().rail(), id).unseenResult);
    EXPECT_EQ(railItem(*window().rail(), id).runPhase, an::ui::RailRunPhase::Succeeded);
    EXPECT_TRUE(window().rail()->itemToolTip(id).contains(QStringLiteral("applied")));

    // Show, as a click on the item does: the window is raised and the result seen.
    const QRect place = window().rail()->itemRect(id);
    ASSERT_TRUE(place.isValid());
    QTest::mouseClick(window().rail(), Qt::LeftButton, Qt::NoModifier, place.center());
    settle();
    EXPECT_EQ(tasks().model().current(), taskId);
    EXPECT_FALSE(railItem(*window().rail(), id).unseenResult);
    EXPECT_EQ(railItem(*window().rail(), id).runPhase, an::ui::RailRunPhase::None);
    EXPECT_TRUE(process->isVisible());

    activateMainWindow();
    (void)tasks().model().setCurrent(0u);
    process->setRunState(an::ui::runningState());
    process->setRunState(ProcessRunState{.phase = Phase::Failed, .text = QStringLiteral("out of memory")});
    EXPECT_EQ(railItem(*window().rail(), id).runPhase, an::ui::RailRunPhase::Failed);
    EXPECT_TRUE(railItem(*window().rail(), id).unseenResult);
}

// A task's menu: Show; Attach, which a process shown as a window cannot do yet; Close, refused during a run.
TEST_F(Tasks, TheMenusEntries)
{
    ImageViewControl* const view = openView(QStringLiteral("tasks-menu"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kWindowProcess);
    ASSERT_NE(process, nullptr);
    const TaskId taskId = taskOf(process)->id;

    {
        QMenu menu;
        window().buildTaskMenu(menu, taskId);
        QStringList texts;
        for (const QAction* action : menu.actions())
        {
            texts << (action->isSeparator() ? QStringLiteral("|") : action->text());
        }
        EXPECT_EQ(texts, (QStringList{QStringLiteral("Show"), QStringLiteral("Attach"), QStringLiteral("|"),
                                      QStringLiteral("Close")}));
        EXPECT_FALSE(entryOf(menu, QStringLiteral("Attach"))->isEnabled());
        EXPECT_TRUE(entryOf(menu, QStringLiteral("Close"))->isEnabled());
    }
    {
        process->setRunState(an::ui::runningState());
        QMenu menu;
        window().buildTaskMenu(menu, taskId);
        EXPECT_FALSE(entryOf(menu, QStringLiteral("Close"))->isEnabled()) << "not during a run";
        process->setRunState(ProcessRunState{.phase = Phase::Cancelled});
    }
    {
        QMenu menu;
        window().buildTaskMenu(menu, taskId);
        const QPointer<TopLevelProcessBaseControl> closing(process);
        entryOf(menu, QStringLiteral("Close"))->trigger();
        settle();
        EXPECT_TRUE(closing.isNull());
        EXPECT_EQ(tasks().model().task(taskId), nullptr);
    }
}

// A real Apply goes through the rail: the task runs, and its result is left to see.
TEST_F(Tasks, AnApplyRunsAndEndsInTheRail)
{
    ImageViewControl* const view = openView(QStringLiteral("tasks-apply"));
    ASSERT_NE(view, nullptr);
    auto* const process = dynamic_cast<ImageRTPreviewControl*>(launch(kBrightnessContrast));
    ASSERT_NE(process, nullptr);
    const TaskId taskId = taskOf(process)->id;
    activateMainWindow();
    if (tasks().model().current() == taskId)
    {
        (void)tasks().model().setCurrent(0u);
    }
    std::vector<Phase> seen;
    const std::size_t token = tasks().model().subscribe([&](an::ui::shell::TaskModel::Change, const TaskId id) {
        if (const Task* task = tasks().model().task(id);
            task != nullptr && id == taskId && (seen.empty() || seen.back() != task->run.phase))
        {
            seen.push_back(task->run.phase);
        }
    });

    const QPointer<ImageRTPreviewControl> guard(process);
    process->applyProcess();
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return guard.isNull() || guard->isProcessIdle(); }));
    settle();
    tasks().model().unsubscribe(token);
    EXPECT_EQ(seen, (std::vector<Phase>{Phase::Running, Phase::Succeeded}));
    EXPECT_FALSE(view->isLockedForProcessing());
    EXPECT_EQ(noticeOf(*view), nullptr) << "the notice went with the lock";
}

// While a run writes an image its view carries the notice: the task's name, how far it has come, and Show.
TEST_F(BlockedView, TheNoticeNamesTheProcessAndTheProgress)
{
    ImageViewControl* const view = openView(QStringLiteral("blocked-notice"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kTargetProcess);
    ASSERT_NE(process, nullptr);

    // The framework takes the lock first and states the run after it, as an Apply does.
    ASSERT_TRUE(view->tryRequestLockForProcessing());
    settle();
    ASSERT_NE(noticeOf(*view), nullptr);
    process->setRunState(an::ui::runningState({}, 0.42));
    const an::ui::ViewProcessingNotice* notice = noticeOf(*view);
    ASSERT_NE(notice, nullptr);
    EXPECT_EQ(notice->notice().text, QStringLiteral("DeepSNR is applying"));
    EXPECT_EQ(notice->notice().progress, std::optional<double>(0.42));
    EXPECT_EQ(notice->notice().actionLabel, QStringLiteral("Show"));
    process->reportRunProgress(0.75);
    EXPECT_EQ(noticeOf(*view)->notice().progress, std::optional<double>(0.75));

    process->setRunState(ProcessRunState{.phase = Phase::Succeeded});
    view->releaseLockForProcessing();
    settle();
    EXPECT_EQ(noticeOf(*view), nullptr);
}

// A run that writes its image without taking the view's lock - an edit of a still image takes the image's writer -
// blocks the view all the same, for as long as it runs.
TEST_F(BlockedView, ARunBlocksItsViewWithoutTheLock)
{
    ImageViewControl* const view = openView(QStringLiteral("blocked-unlocked"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kTargetProcess);
    ASSERT_NE(process, nullptr);
    ASSERT_EQ(noticeOf(*view), nullptr) << "an image is not blocked because a task is open on it";

    process->setRunState(an::ui::runningState({}, 0.3));
    ASSERT_FALSE(view->isLockedForProcessing());
    const an::ui::ViewProcessingNotice* notice = noticeOf(*view);
    ASSERT_NE(notice, nullptr);
    EXPECT_EQ(notice->notice().text, QStringLiteral("DeepSNR is applying"));
    EXPECT_EQ(notice->notice().progress, std::optional<double>(0.3));

    process->setRunState(ProcessRunState{.phase = Phase::Succeeded});
    settle();
    EXPECT_EQ(noticeOf(*view), nullptr) << "the notice goes as the run ends";
}

// The notice's Show goes to the task: its window raised, and the task the current one.
TEST_F(BlockedView, ShowRaisesTheTask)
{
    ImageViewControl* const view = openView(QStringLiteral("blocked-show"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kTargetProcess);
    ASSERT_NE(process, nullptr);
    const TaskId taskId = taskOf(process)->id;
    activateMainWindow();
    (void)tasks().model().setCurrent(0u);

    process->setRunState(an::ui::runningState());
    ASSERT_TRUE(view->tryRequestLockForProcessing());
    settle();
    const an::ui::ViewProcessingNotice* notice = noticeOf(*view);
    ASSERT_NE(notice, nullptr);
    ASSERT_TRUE(notice->notice().action);
    notice->notice().action();
    settle();
    EXPECT_EQ(tasks().model().current(), taskId);
    EXPECT_TRUE(process->isVisible());

    process->setRunState(ProcessRunState{.phase = Phase::Cancelled});
    view->releaseLockForProcessing();
    settle();
}

// Another view goes on as usual while one is blocked: no notice of its own, and a process opens over it.
TEST_F(BlockedView, AnotherViewTakesAProcessMeanwhile)
{
    ImageViewControl* const blocked = openView(QStringLiteral("blocked-one"));
    ImageViewControl* const free = openView(QStringLiteral("blocked-other"));
    ASSERT_TRUE(blocked != nullptr && free != nullptr);
    core().viewService().setActiveImageViewControl(blocked);
    TopLevelProcessBaseControl* const running = launch(kTargetProcess);
    ASSERT_NE(running, nullptr);
    running->setRunState(an::ui::runningState());
    ASSERT_TRUE(blocked->tryRequestLockForProcessing());
    settle();
    ASSERT_NE(noticeOf(*blocked), nullptr);

    // The person goes back to the images, and works on another.
    window().showViews();
    settle();
    core().viewService().setActiveImageViewControl(free);
    settle();
    TopLevelProcessBaseControl* const other = launch(kTargetProcess);
    ASSERT_NE(other, nullptr);
    EXPECT_EQ(other->processTarget(), free);
    EXPECT_EQ(noticeOf(*free), nullptr);
    EXPECT_NE(noticeOf(*blocked), nullptr) << "the blocked view stays blocked";
    EXPECT_EQ(tasks().model().tasksOn(free).size(), 1u);

    running->setRunState(ProcessRunState{.phase = Phase::Cancelled});
    blocked->releaseLockForProcessing();
    settle();
}

// The tab of an image a task is open on carries the task's glyph: its tooltip names the task, a run locks it, a click
// goes to the task, and it goes with the task.
TEST_F(TabGlyph, ATaskMarksItsImagesTab)
{
    ImageViewControl* const view = openView(QStringLiteral("tab-glyph"));
    ASSERT_NE(view, nullptr);
    ASSERT_NE(view->getDockWidget(), nullptr);
    EXPECT_EQ(an::ui::tabAdornment(view->getDockWidget()), nullptr);
    TopLevelProcessBaseControl* const process = launch(kTargetProcess);
    ASSERT_NE(process, nullptr);
    const Task* task = taskOf(process);
    ASSERT_NE(task, nullptr);
    const TaskId taskId = task->id;

    const an::ui::TabAdornment* glyph = an::ui::tabAdornment(view->getDockWidget());
    ASSERT_NE(glyph, nullptr);
    EXPECT_EQ(glyph->iconKey, task->iconKey);
    EXPECT_EQ(glyph->toolTip, QStringLiteral("DeepSNR is open on this image. Click to go to the task"));
    EXPECT_FALSE(glyph->locked);

    process->setRunState(an::ui::runningState());
    ASSERT_TRUE(view->tryRequestLockForProcessing());
    settle();
    glyph = an::ui::tabAdornment(view->getDockWidget());
    ASSERT_NE(glyph, nullptr);
    EXPECT_TRUE(glyph->locked);
    EXPECT_EQ(glyph->toolTip,
              QStringLiteral("DeepSNR is applying to this image: it is locked until the run ends. Click "
                             "to go to the task"));
    process->setRunState(ProcessRunState{.phase = Phase::Cancelled});
    view->releaseLockForProcessing();
    settle();

    activateMainWindow();
    (void)tasks().model().setCurrent(0u);
    glyph = an::ui::tabAdornment(view->getDockWidget());
    ASSERT_NE(glyph, nullptr);
    ASSERT_TRUE(glyph->activated);
    glyph->activated();
    settle();
    EXPECT_EQ(tasks().model().current(), taskId);

    process->close();
    settle();
    EXPECT_EQ(an::ui::tabAdornment(view->getDockWidget()), nullptr);
}

// The history strip of the image shows the open task's pending step, "writing" during a run, and follows the view the
// strip shows.
TEST_F(HistoryStrip, PendingChipFollowsTheTask)
{
    ImageViewControl* const target = openView(QStringLiteral("pending-target"));
    ImageViewControl* const other = openView(QStringLiteral("pending-other"));
    ASSERT_TRUE(target != nullptr && other != nullptr);
    core().viewService().setActiveImageViewControl(target);
    window().showViews();
    settle();
    ASSERT_NE(strip(), nullptr);
    EXPECT_FALSE(strip()->pendingStep().has_value());

    TopLevelProcessBaseControl* const process = launch(kTargetProcess);
    ASSERT_NE(process, nullptr);
    core().viewService().setActiveImageViewControl(target);
    settle();
    ASSERT_EQ(window().workspacePage()->binder().historyView(), target);
    ASSERT_TRUE(strip()->pendingStep().has_value());
    EXPECT_EQ(strip()->pendingStep()->processName, QStringLiteral("DeepSNR"));
    EXPECT_FALSE(strip()->pendingStep()->writing);
    ASSERT_NE(strip()->pendingChip(), nullptr);

    process->setRunState(an::ui::runningState());
    EXPECT_TRUE(strip()->pendingStep()->writing);
    process->setRunState(ProcessRunState{.phase = Phase::Succeeded});
    EXPECT_FALSE(strip()->pendingStep()->writing);

    // The strip shows another view: no task is open on it.
    core().viewService().setActiveImageViewControl(other);
    settle();
    ASSERT_EQ(window().workspacePage()->binder().historyView(), other);
    EXPECT_FALSE(strip()->pendingStep().has_value());
    core().viewService().setActiveImageViewControl(target);
    settle();
    EXPECT_TRUE(strip()->pendingStep().has_value());

    process->close();
    settle();
    EXPECT_FALSE(strip()->pendingStep().has_value());
}

} // namespace an::tests::observatory
