// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A floating task in the Observatory window, as a person meets it: Float as Window - from the task's header, its rail
/// menu or the palette - takes its page out of the centre into a window of its own, at the size its process's window
/// was last given, over the main window's centre; the rail's item dims and the main window shows the place it showed
/// before the task. The window is titled with the process and its image, has one title bar and no tab strip, keeps its
/// least size, and is a window of its own. Attach brings the page back as the current, focused task. Floating and
/// attaching leave a run and its lock as they were; closing the window closes the task, except during a run. And the
/// window's going: closed by its title bar it leaves no drag of the dock library behind, and the application's exit
/// stops a floating task's process, drains the work, deletes the process, and only then its window.

#include "ObservatoryWindowCase.h"

#include "ShellActions.h"
#include "pages/TaskPage.h"
#include "tasks/TaskDrag.h"
#include "tasks/TaskPresenter.h"
#include "tasks/TaskWindow.h"
#include "workspaces/WorkspaceHost.h"

#include "app/ProcessRegistry.h"
#include "commands/CommandIndex.h"
#include "components/AnIconButton.h"
#include "components/AnIconRail.h"
#include "components/AnTaskHeader.h"
#include "components/RailItem.h"
#include "controls/KDDockWidgetsHelper.h"
#include "controls/ProcessLaunchScope.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "core/ApplicationContext.h"
#include "services/ProcessLaunchService.h"
#include "tasks/TaskModel.h"
#include "views/DockHooks.h"

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>

#include <QApplication>
#include <QCursor>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QScreen>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <memory>
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
using an::ui::shell::TaskPresentation;
using Phase = ProcessRunState::Phase;
using Window = an::ui::observatory::ObservatoryWindow;
namespace shellIds = an::ui::observatory::ids;

/// A process launched on an image, a page over a view of it.
constexpr std::string_view kTargetProcess = "denoising.actionDeepSNR";

/// Sizes a task window keeps on any screen that can show one: over what its page needs (its least size is 720 x 480,
/// and the page a font's width and height more) and inside the smallest screen a platform gives (the suites' Linux
/// platform has one of 800 x 800), so no screen bounds them and no content raises them.
constexpr QSize kRemembered{760, 640};
constexpr QSize kResized{780, 660};

void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

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

/// The cases of a floating task: each opens DeepSNR, a page, over a view of its own.
class TaskFloatCase : public ObservatoryWindowCase
{
protected:
    [[nodiscard]] static an::ui::observatory::TaskPresenter& tasks()
    {
        an::ui::observatory::TaskPresenter* presenter = window().taskPresenter();
        EXPECT_NE(presenter, nullptr);
        return *presenter;
    }

    static TopLevelProcessBaseControl* launch(std::string_view descriptorId)
    {
        const std::size_t before = tasks().model().tasks().size();
        EXPECT_TRUE(core().processLaunch().launch(std::string(descriptorId)));
        settle();
        const std::vector<Task>& now = tasks().model().tasks();
        EXPECT_EQ(now.size(), before + 1u);
        return now.size() > before ? now.back().process.data() : nullptr;
    }

    /// Opens an image and DeepSNR over it, its page shown; the task's id, zero when it failed.
    static TaskId openTask(const QString& image, TopLevelProcessBaseControl** opened = nullptr)
    {
        ImageViewControl* const view = openView(image);
        EXPECT_NE(view, nullptr);
        TopLevelProcessBaseControl* const process = launch(kTargetProcess);
        EXPECT_NE(process, nullptr);
        if (opened != nullptr)
        {
            *opened = process;
        }
        const Task* task = process != nullptr ? tasks().model().taskFor(process) : nullptr;
        EXPECT_TRUE(window().isTaskPageShown());
        return task != nullptr ? task->id : TaskId{0};
    }

    [[nodiscard]] static an::ui::RailItem railItem(const TaskId id)
    {
        for (const an::ui::RailItem& item : window().rail()->items())
        {
            if (item.id == Window::railTaskId(id))
            {
                return item;
            }
        }
        return an::ui::RailItem{};
    }

    /// The size a process's window was last given, as its process settings keep it; its settings kept as they are.
    static void remember(const TopLevelProcessBaseControl& process, const QSize& size)
    {
        an::ui::IProcessSettingsProvider* const settings = core().context()->getProcessSettings();
        int state = Qt::WindowNoState;
        int width = -1;
        int height = -1;
        QString json;
        (void)settings->select(QString::fromStdString(process.processName()), state, width, height, json);
        ASSERT_TRUE(settings->upsert(QString::fromStdString(process.processName()), Qt::WindowNoState, size.width(),
                                     size.height(), json));
    }

    [[nodiscard]] static QSize remembered(const TopLevelProcessBaseControl& process)
    {
        an::ui::IProcessSettingsProvider* const settings = core().context()->getProcessSettings();
        int state = Qt::WindowNoState;
        int width = -1;
        int height = -1;
        QString json;
        if (!settings->select(QString::fromStdString(process.processName()), state, width, height, json))
        {
            return {};
        }
        return QSize(width, height);
    }

    /// The centre on the screen.
    [[nodiscard]] static QRect centreOnScreen()
    {
        return QRect(window().centre()->mapToGlobal(QPoint(0, 0)), window().centre()->size());
    }
};

class TaskFloat : public TaskFloatCase
{
};

/// A process of the suite's own, shown as a full page, that writes down when it is asked to stop and when it goes.
class ExitOrderProcess final : public TopLevelProcessBaseControl
{
public:
    explicit ExitOrderProcess(std::vector<std::string>& events)
        : TopLevelProcessBaseControl("Exit Order Process"), m_events(events)
    {
        auto* layout = new QVBoxLayout(this);
        auto* body = new QLabel(QStringLiteral("exit order"), this);
        body->setMinimumSize(300, 200);
        layout->addWidget(body);
    }

    ~ExitOrderProcess() override
    {
        m_events.emplace_back("process deleted");
    }

    ExitOrderProcess(const ExitOrderProcess&) = delete;
    ExitOrderProcess& operator=(const ExitOrderProcess&) = delete;

protected:
    void onStopWorkInFlight() override
    {
        m_events.emplace_back("process stopped");
    }

private:
    std::vector<std::string>& m_events;
};

} // namespace

// Float as Window takes the page out of the centre into a window of its own at the size its process's window was last
// given, over the main window's centre; the rail's item dims and the header offers Attach; the main window shows the
// place it showed before the task.
TEST_F(TaskFloat, FloatingTakesThePageOutAtTheRememberedSizeAndDimsTheRailItem)
{
    TopLevelProcessBaseControl* process = nullptr;
    const TaskId id = openTask(QStringLiteral("float-out"), &process);
    ASSERT_NE(id, 0u);
    ASSERT_NE(process, nullptr);
    remember(*process, kRemembered);
    an::ui::observatory::TaskPage* const page = window().taskPage(id);
    ASSERT_NE(page, nullptr);
    ASSERT_TRUE(railItem(id).selected);

    page->header()->floatButton()->click();
    settle();

    EXPECT_FALSE(window().isTaskPageShown());
    EXPECT_TRUE(window().isWorkspaceShown()) << "the place shown before the task: the image's workspace";
    EXPECT_EQ(window().centre()->indexOf(page), -1) << "the page left the centre";
    an::ui::observatory::TaskWindow* const floating = window().taskWindow(id);
    ASSERT_NE(floating, nullptr);
    ASSERT_TRUE(floating->isOpen());
    EXPECT_EQ(floating->page(), page);
    ASSERT_NE(floating->window(), nullptr);
    EXPECT_EQ(floating->window()->geometry().size(), kRemembered) << "the size the process was last given";
    const QPoint middle = floating->window()->geometry().center();
    EXPECT_LE((middle - centreOnScreen().center()).manhattanLength(), 2) << "over the main window's centre";
    EXPECT_TRUE(process->isVisible());
    EXPECT_TRUE(page->isAncestorOf(process));

    const an::ui::RailItem item = railItem(id);
    EXPECT_TRUE(item.floating) << "the rail's item is dimmed";
    EXPECT_FALSE(item.selected);
    EXPECT_TRUE(page->header()->isFloating()) << "its header offers Attach";
    EXPECT_EQ(tasks().model().task(id)->presentation, TaskPresentation::Window);
}

// The window is titled with the process and its image, has one title bar and no tab strip, is never smaller than its
// least size, and is a window of its own - not a tool window, in which the main window's shortcuts would act.
TEST_F(TaskFloat, TheWindowIsTitledHasOneTitleBarAndKeepsItsLeastSize)
{
    TopLevelProcessBaseControl* process = nullptr;
    const TaskId id = openTask(QStringLiteral("float-title"), &process);
    ASSERT_NE(process, nullptr);
    remember(*process, QSize(400, 300));
    ASSERT_TRUE(window().floatTask(id));
    settle();
    an::ui::observatory::TaskWindow* const floating = window().taskWindow(id);
    ASSERT_NE(floating, nullptr);
    QWidget* const frame = floating->window();
    ASSERT_NE(frame, nullptr);

    EXPECT_EQ(floating->title(), QStringLiteral("DeepSNR - float-title"));
    EXPECT_EQ(frame->windowTitle(), QStringLiteral("DeepSNR - float-title"));
    EXPECT_GE(frame->width(), an::ui::observatory::TaskWindow::kMinimumWidth) << "a size under the least is raised";
    EXPECT_GE(frame->height(), an::ui::observatory::TaskWindow::kMinimumHeight);
    EXPECT_GE(frame->minimumWidth(), an::ui::observatory::TaskWindow::kMinimumWidth);
    EXPECT_GE(frame->minimumHeight(), an::ui::observatory::TaskWindow::kMinimumHeight);
    EXPECT_EQ(frame->windowType(), Qt::Window) << "a window of its own";
    for (const AN_TabBar* strip : frame->findChildren<AN_TabBar*>())
    {
        EXPECT_FALSE(strip->isVisible()) << "no tab strip";
    }
    int titleBars = 0;
    for (const AN_TitleBar* titleBar : frame->findChildren<AN_TitleBar*>())
    {
        titleBars += titleBar->isVisible() ? 1 : 0;
    }
    EXPECT_LE(titleBars, 1) << "one title bar: the system's, or the library's own";
    EXPECT_EQ(floating->dockWidget()->affinities(), QStringList{QStringLiteral("an.task")});
    EXPECT_EQ(floating->dockWidget()->uniqueName(), an::ui::observatory::TaskWindow::dockName(id));
}

// Attach brings the page back into the centre as the current task, its rail item selected and no longer dimmed, the
// focus in its page; the window goes.
TEST_F(TaskFloat, AttachingBringsItBackAsTheCurrentFocusedTask)
{
    TopLevelProcessBaseControl* process = nullptr;
    const TaskId id = openTask(QStringLiteral("float-attach"), &process);
    ASSERT_NE(process, nullptr);
    an::ui::observatory::TaskPage* const page = window().taskPage(id);
    ASSERT_NE(page, nullptr);
    ASSERT_TRUE(window().floatTask(id));
    settle();
    ASSERT_NE(window().taskWindow(id), nullptr);

    page->header()->floatButton()->click();
    settle();

    EXPECT_EQ(window().taskWindow(id), nullptr) << "the window goes";
    EXPECT_TRUE(window().isTaskPageShown());
    EXPECT_EQ(window().shownTask(), id);
    EXPECT_EQ(window().centre()->currentWidget(), page);
    EXPECT_EQ(tasks().model().current(), id);
    EXPECT_EQ(tasks().model().task(id)->presentation, TaskPresentation::Page);
    EXPECT_FALSE(page->header()->isFloating());
    EXPECT_FALSE(railItem(id).floating);
    EXPECT_TRUE(railItem(id).selected);
    EXPECT_TRUE(page->isAncestorOf(process));
    // The focus is the page's: the window's focus widget, which has the focus whenever the window is active.
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() {
        const QWidget* const focus = window().focusWidget();
        return focus != nullptr && page->isAncestorOf(focus);
    })) << "the focus is in the page";
    if (window().isActiveWindow())
    {
        EXPECT_TRUE(QApplication::focusWidget() != nullptr && page->isAncestorOf(QApplication::focusWidget()));
    }
}

// Floating and attaching move the page, nothing in it: a run on its way and the lock it holds on the image are as they
// were, the rail and the header still tell the run, and a run that ends while the task floats is told in its header.
TEST_F(TaskFloat, FloatingAndAttachingDuringARunLeaveTheRunAndTheLock)
{
    TopLevelProcessBaseControl* process = nullptr;
    const TaskId id = openTask(QStringLiteral("float-run"), &process);
    ASSERT_NE(process, nullptr);
    ImageViewControl* const view = tasks().model().task(id)->target.data();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(view->tryRequestLockForProcessing());
    process->setRunState(an::ui::runningState({}, 0.42));
    an::ui::observatory::TaskPage* const page = window().taskPage(id);
    ASSERT_NE(page, nullptr);

    ASSERT_TRUE(window().floatTask(id));
    settle();
    EXPECT_TRUE(process->runState().running());
    EXPECT_TRUE(view->isLockedForProcessing());
    EXPECT_EQ(railItem(id).runPhase, an::ui::RailRunPhase::Running);
    EXPECT_EQ(railItem(id).progress, std::optional<int>(42));
    EXPECT_EQ(page->header()->state(), an::ui::AnTaskHeader::State::Running);

    ASSERT_TRUE(window().attachTask(id));
    settle();
    EXPECT_TRUE(process->runState().running());
    EXPECT_TRUE(view->isLockedForProcessing());
    EXPECT_EQ(railItem(id).runPhase, an::ui::RailRunPhase::Running);
    EXPECT_EQ(page->header()->state(), an::ui::AnTaskHeader::State::Running);

    view->releaseLockForProcessing();
    process->setRunState(ProcessRunState{.phase = Phase::Cancelled});

    // A run that succeeds while the task floats is told in its header, as on the page.
    ASSERT_TRUE(window().floatTask(id));
    settle();
    process->setRunState(an::ui::runningState());
    process->setRunState(ProcessRunState{.phase = Phase::Succeeded});
    EXPECT_EQ(page->header()->state(), an::ui::AnTaskHeader::State::Applied);
    EXPECT_TRUE(page->header()->stateText().startsWith(QStringLiteral("applied")))
        << page->header()->stateText().toStdString();
}

// Closing a floating task's window closes the task: its process goes, and its rail item. During a run the window
// stays, as the task's Close is refused then.
TEST_F(TaskFloat, ClosingTheWindowClosesTheTaskButNotDuringARun)
{
    TopLevelProcessBaseControl* process = nullptr;
    const TaskId id = openTask(QStringLiteral("float-close"), &process);
    ASSERT_NE(process, nullptr);
    ASSERT_TRUE(window().floatTask(id));
    settle();
    an::ui::observatory::TaskWindow* const floating = window().taskWindow(id);
    ASSERT_NE(floating, nullptr);
    ASSERT_NE(floating->window(), nullptr);

    process->setRunState(an::ui::runningState());
    floating->window()->close();
    settle();
    ASSERT_NE(window().taskWindow(id), nullptr) << "not during a run";
    EXPECT_TRUE(window().taskWindow(id)->isOpen());
    EXPECT_NE(tasks().model().task(id), nullptr);
    process->setRunState(ProcessRunState{.phase = Phase::Cancelled});

    const QPointer<TopLevelProcessBaseControl> closing(process);
    window().taskWindow(id)->window()->close();
    settle();
    EXPECT_TRUE(closing.isNull()) << "the process goes";
    EXPECT_EQ(tasks().model().task(id), nullptr);
    EXPECT_EQ(window().taskWindow(id), nullptr);
    EXPECT_TRUE(railItem(id).id.isEmpty()) << "and its rail item";
}

// The rail menu offers Float as Window for a task in the centre and Attach for a floating one; the palette offers Float
// as Window while a task's page is shown; the main window's Window menu holds both and Close Task.
TEST_F(TaskFloat, TheRailMenuAndThePaletteFloatAndAttach)
{
    TopLevelProcessBaseControl* process = nullptr;
    const TaskId id = openTask(QStringLiteral("float-menus"), &process);
    ASSERT_NE(process, nullptr);
    {
        QMenu menu;
        window().buildTaskMenu(menu, id);
        QAction* const place = entryOf(menu, QStringLiteral("Float as Window"));
        ASSERT_NE(place, nullptr);
        EXPECT_TRUE(place->isEnabled());
        place->trigger();
        settle();
    }
    ASSERT_NE(window().taskWindow(id), nullptr);
    {
        QMenu menu;
        window().buildTaskMenu(menu, id);
        QAction* const place = entryOf(menu, QStringLiteral("Attach"));
        ASSERT_NE(place, nullptr);
        EXPECT_TRUE(place->isEnabled());
        place->trigger();
        settle();
    }
    ASSERT_EQ(window().taskWindow(id), nullptr);
    ASSERT_TRUE(window().isTaskPageShown());

    // The palette runs the shell's Float as Window.
    core().commands().refresh();
    const QString floatId = QStringLiteral("action:") + QString::fromLatin1(shellIds::kFloatTask.data());
    const an::ui::shell::CommandEntry* const entry = core().commands().entry(floatId);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->title, QStringLiteral("Float as Window"));
    EXPECT_TRUE(entry->available);
    EXPECT_TRUE(core().commands().run(floatId));
    settle();
    EXPECT_NE(window().taskWindow(id), nullptr);

    QAction* const attach = window().shellActions().action(shellIds::kAttachTask);
    QAction* const closeTask = window().shellActions().action(shellIds::kCloseTask);
    ASSERT_NE(attach, nullptr);
    ASSERT_NE(closeTask, nullptr);
    EXPECT_EQ(attach->text(), QStringLiteral("Attach"));
    EXPECT_EQ(closeTask->text(), QStringLiteral("Close Task"));
    ASSERT_TRUE(window().attachTask(id));
    settle();
    EXPECT_TRUE(window().shellActions().action(shellIds::kFloatTask)->isEnabled());
    EXPECT_TRUE(closeTask->isEnabled());
    const QPointer<TopLevelProcessBaseControl> closing(process);
    closeTask->trigger();
    settle();
    EXPECT_TRUE(closing.isNull()) << "Close Task closes the current task";
}

// A click on a floating task's rail item raises its window, and the task whose window is active is the current one;
// the main window active again, it is not.
TEST_F(TaskFloat, TheRailRaisesAFloatingTaskWhoseWindowMakesItCurrent)
{
    const TaskId id = openTask(QStringLiteral("float-raise"));
    ASSERT_TRUE(window().floatTask(id));
    settle();
    an::ui::observatory::TaskWindow* const floating = window().taskWindow(id);
    ASSERT_NE(floating, nullptr);
    (void)tasks().model().setCurrent(0u);

    const QRect place = window().rail()->itemRect(Window::railTaskId(id));
    ASSERT_TRUE(place.isValid());
    QTest::mouseClick(window().rail(), Qt::LeftButton, Qt::NoModifier, place.center());
    settle();
    EXPECT_EQ(tasks().model().current(), id);
    EXPECT_FALSE(window().isTaskPageShown()) << "the page stays in its window";
    EXPECT_NE(window().taskWindow(id), nullptr);

    tasks().activeWindowChanged(&window());
    EXPECT_EQ(tasks().model().current(), 0u) << "another window is the one looked at";
    tasks().activeWindowChanged(floating->window());
    EXPECT_EQ(tasks().model().current(), id);
}

// The size a person gives the window is the process's: kept when the task attaches, and the next float shows it.
TEST_F(TaskFloat, TheWindowsSizeIsRememberedForTheNextFloat)
{
    TopLevelProcessBaseControl* process = nullptr;
    const TaskId id = openTask(QStringLiteral("float-size"), &process);
    ASSERT_NE(process, nullptr);
    remember(*process, kRemembered);
    ASSERT_TRUE(window().floatTask(id));
    settle();
    QWidget* const frame = window().taskWindow(id)->window();
    ASSERT_NE(frame, nullptr);
    frame->resize(kResized);
    settle();
    ASSERT_TRUE(window().attachTask(id));
    settle();
    EXPECT_EQ(remembered(*process), kResized);

    ASSERT_TRUE(window().floatTask(id));
    settle();
    ASSERT_NE(window().taskWindow(id), nullptr);
    EXPECT_EQ(window().taskWindow(id)->window()->geometry().size(), kResized);
}

// A window is held within the screen the main window is on, as a process window's remembered size is: a size given on a
// larger screen is the screen's here. A screen too small for a task window's least size bounds nothing.
TEST_F(TaskFloat, AWindowIsHeldWithinTheScreenItIsShownOn)
{
    const QScreen* const screen = window().screen();
    ASSERT_NE(screen, nullptr);
    const QSize available = screen->availableGeometry().size();
    if (available.width() < an::ui::observatory::TaskWindow::kMinimumWidth ||
        available.height() < an::ui::observatory::TaskWindow::kMinimumHeight)
    {
        GTEST_SKIP() << "the platform's screen is smaller than a task window's least size: nothing is bounded";
    }
    TopLevelProcessBaseControl* process = nullptr;
    const TaskId id = openTask(QStringLiteral("float-screen"), &process);
    ASSERT_NE(process, nullptr);
    remember(*process, available + QSize(240, 160));
    ASSERT_TRUE(window().floatTask(id));
    settle();
    ASSERT_NE(window().taskWindow(id), nullptr);
    EXPECT_EQ(window().taskWindow(id)->window()->geometry().size(), available);
}

// A press on a task window's title bar leaves the dock library waiting for a drag; the window closed by its title bar
// meanwhile closes the task and leaves no drag behind: the next move of the mouse over what the library drags by
// starts none, nothing is logged of a dragged window that went, and the next drag starts.
TEST_F(TaskFloat, ClosedByItsTitleBarTheWindowLeavesNoDragBehind)
{
    const TaskId first = openTask(QStringLiteral("window-pressed"));
    ASSERT_TRUE(window().floatTask(first));
    settle();
    an::ui::observatory::TaskWindow* const floating = window().taskWindow(first);
    ASSERT_NE(floating, nullptr);
    QWidget* const frame = floating->window();
    ASSERT_NE(frame, nullptr);

    static QStringList s_logged;
    s_logged.clear();
    const QtMessageHandler before = qInstallMessageHandler(
        [](QtMsgType, const QMessageLogContext&, const QString& message) { s_logged.append(message); });
    // Every drag the library is about to start is told to the dock hooks.
    int dragsAsked = 0;
    an::ui::shell::DockHooks::Subscriber counting;
    counting.dragAboutToStart = [&dragsAsked](const an::ui::shell::DockDrag& /*drag*/) {
        ++dragsAsked;
        return true;
    };
    an::ui::shell::DockHooks::Subscription counted = an::ui::shell::DockHooks::subscribe(std::move(counting));

    // Pressed on its title bar, and closed by it: the system's close of a window whose title bar is the system's.
    const QPoint grip = frame->mapToGlobal(QPoint(24, 8));
    QMouseEvent press(QEvent::NonClientAreaMouseButtonPress, QPointF(24, 8), QPointF(grip), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    const QPointer<QWidget> gone(frame);
    QCoreApplication::sendEvent(frame, &press);
    frame->close();
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return gone.isNull(); })) << "the window goes with its task";
    // The next move of the mouse, over what the dock library drags by and so hears: the image's tab strip.
    QWidget* strip = nullptr;
    for (AN_TabBar* tabBar : window().workspaces().mainWindow()->findChildren<AN_TabBar*>())
    {
        if (tabBar->isVisible())
        {
            strip = tabBar;
        }
    }
    ASSERT_NE(strip, nullptr);
    const QPoint over = strip->mapToGlobal(strip->rect().center());
    QMouseEvent move(QEvent::MouseMove, QPointF(strip->mapFromGlobal(over)), QPointF(over), Qt::NoButton, Qt::NoButton,
                     Qt::NoModifier);
    QCoreApplication::sendEvent(strip, &move);
    settle();
    EXPECT_EQ(tasks().model().task(first), nullptr) << "the task closed";
    EXPECT_EQ(dragsAsked, 0) << "the press on the title bar of the window that went starts no drag";
    counted.reset();

    // The next drag of a task's window starts.
    const TaskId second = openTask(QStringLiteral("window-next"));
    ASSERT_TRUE(window().floatTask(second));
    settle();
    an::ui::observatory::TaskWindow* const next = window().taskWindow(second);
    ASSERT_NE(next, nullptr);
    QCursor::setPos(next->window()->mapToGlobal(QPoint(24, 8)));
    EXPECT_TRUE(next->dockWidget()->dockWidget()->startDragging()) << "no drag was left behind";
    QMouseEvent release(QEvent::NonClientAreaMouseButtonRelease, QPointF(24, 8),
                        QPointF(next->window()->mapToGlobal(QPoint(24, 8))), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(next->window(), &release);
    settle();
    qInstallMessageHandler(before);

    for (const QString& line : s_logged)
    {
        EXPECT_FALSE(line.contains(QStringLiteral("Draggable was destroyed"))) << line.toStdString();
    }
}

/// A floating task's window at the application's exit, a start of its own.
class TaskWindow : public ::testing::Test
{
};

// Closing the main window with a task floating closes it in the order process windows close: the process is asked to
// stop, the work is drained, the process is deleted, and its window only then.
TEST_F(TaskWindow, MainWindowCloseFollowsTheExitOrder)
{
    std::vector<std::string> events;
    std::unique_ptr<RunningObservatory> running = startObservatory();
    settle();
    Window& main = *running->shell->window();

    an::ui::process_registry::ProcessDescriptor descriptor;
    descriptor.id = "tests.exit_order";
    descriptor.display_name = "Exit Order Process";
    descriptor.process_name = "Exit Order Process";
    descriptor.category = "multi_image_stacking";
    descriptor.action_object_name = "actionNoSuchExitOrder";
    descriptor.launch_mode = an::ui::process_registry::ProcessLaunchMode::TopLevel;
    descriptor.surface = an::ui::ProcessSurface::FullPage;
    ExitOrderProcess* process = nullptr;
    {
        const an::ui::ProcessLaunchScope launch(descriptor);
        process = new ExitOrderProcess(events);
    }
    process->show();
    settle();
    ASSERT_TRUE(process->isPresentedAsPage());
    const Task* task = main.taskPresenter()->model().taskFor(process);
    ASSERT_NE(task, nullptr);
    ASSERT_TRUE(main.floatTask(task->id));
    settle();
    an::ui::observatory::TaskWindow* const floating = main.taskWindow(task->id);
    ASSERT_NE(floating, nullptr);
    ASSERT_NE(floating->window(), nullptr);
    QObject::connect(floating->window(), &QObject::destroyed, [&events]() { events.emplace_back("window deleted"); });

    const auto answered = std::make_shared<an::tests::app::Answered>();
    an::tests::app::answerEveryMessageBox(answered);
    main.close();
    settle();
    const an::Status shut = stopObservatory(std::move(running));
    EXPECT_TRUE(shut.ok()) << shut.message();
    settle();

    EXPECT_EQ(events, (std::vector<std::string>{"process stopped", "process deleted", "window deleted"}));
}

} // namespace an::tests::observatory
