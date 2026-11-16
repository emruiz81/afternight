// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A task dragged between the rail and a window of its own, decided from where the cursor stands against the main
/// window: a thumbnail pressed and moved less than the drag distance does nothing; past it a ghost follows the cursor
/// inside the window; crossing the window's frame floats the task under the cursor and the dock library's drag follows
/// it; a release inside changes nothing. A task's window dragged over the rail or the centre covers that zone with the
/// attach overlay and marks its place in the rail, and attaches when it is released there; released over the top bar,
/// the column or outside it stays a window. Where a window cannot follow the cursor the task floats at the release.
/// The decisions are proved with an injected cursor and geometry, the rail's signals with the rail itself, and what the
/// window does with each decision in the Observatory window; and a trial with a real mouse, asked for by a variable.

#include "ObservatoryWindowCase.h"

#include "pages/TaskPage.h"
#include "tasks/TaskDrag.h"
#include "tasks/TaskPresenter.h"
#include "tasks/TaskWindow.h"

#include "components/AnIconRail.h"
#include "components/AnTaskHeader.h"
#include "components/RailItem.h"
#include "controls/KDDockWidgetsHelper.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "services/ProcessLaunchService.h"
#include "tasks/TaskModel.h"
#include "views/DockHooks.h"

#include <kddockwidgets/DockWidget.h>

#include <QApplication>
#include <QCursor>
#include <QEventLoop>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QScreen>
#include <QStackedWidget>
#include <QTimer>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(Q_OS_WIN)
#include <Windows.h>
#endif

namespace an::tests::observatory
{
namespace
{

using Drag = an::ui::observatory::TaskDrag;
using an::ui::observatory::AttachZone;
using an::ui::shell::Task;
using an::ui::shell::TaskId;
using an::ui::shell::TaskPresentation;
using Window = an::ui::observatory::ObservatoryWindow;

/// A process launched on an image, a page over a view of it.
constexpr std::string_view kTargetProcess = "denoising.actionDeepSNR";

constexpr TaskId kTask = 7u;

/// The variable that asks for the trial with a real mouse: a number of seconds for a person, or, on Windows, `auto`.
constexpr char kHandTrialVariable[] = "AFTERNIGHT_TEST_TASK_DRAG_HAND_TRIAL";

void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// A main window as a drag sees it, and what the drag did to it. The window is 1000 x 700 at (100, 80): a 40 px top
/// bar, the rail its left 44 px under it, the centre beside the rail, a 360 px column at its right and a 28 px status
/// line at its foot.
class FakeSurface final : public Drag::Surface
{
public:
    struct Floated
    {
        TaskId id = 0;
        QPoint at;
        bool follow = false;
    };

    QRect frameRect{100, 80, 1000, 700};
    QRect railRect{100, 120, 44, 632};
    QRect centreRect{144, 120, 596, 632};
    bool follows = true;
    bool libraryTakesTheDrag = true;
    std::vector<Floated> floated;
    std::vector<TaskId> attached;
    std::vector<std::pair<AttachZone, TaskId>> zones;
    int railDragsEnded = 0;
    std::function<void()> onEndRailDrag;

    [[nodiscard]] QRect frame() const override
    {
        return frameRect;
    }
    [[nodiscard]] QRect rail() const override
    {
        return railRect;
    }
    [[nodiscard]] QRect centre() const override
    {
        return centreRect;
    }
    [[nodiscard]] bool windowFollowsTheCursor() const override
    {
        return follows;
    }
    bool floatUnder(const TaskId id, const QPoint globalPos, const bool follow) override
    {
        floated.push_back(Floated{.id = id, .at = globalPos, .follow = follow});
        return follow && libraryTakesTheDrag;
    }
    void attach(const TaskId id) override
    {
        attached.push_back(id);
    }
    void showAttachZone(const AttachZone zone, const TaskId id) override
    {
        zones.emplace_back(zone, id);
    }
    void endRailDrag() override
    {
        ++railDragsEnded;
        if (onEndRailDrag)
        {
            onEndRailDrag();
        }
    }

    // Places on the screen.
    [[nodiscard]] QPoint overTheRail() const
    {
        return railRect.center();
    }
    [[nodiscard]] QPoint overTheCentre() const
    {
        return centreRect.center();
    }
    [[nodiscard]] QPoint overTheTopBar() const
    {
        return QPoint(frameRect.center().x(), frameRect.top() + 20);
    }
    [[nodiscard]] QPoint overTheColumn() const
    {
        return QPoint(centreRect.right() + 180, centreRect.center().y());
    }
    [[nodiscard]] QPoint outside() const
    {
        return QPoint(frameRect.right() + 150, frameRect.center().y());
    }
};

/// Sends a mouse event to a widget at a place inside it, as a person's would arrive.
void sendMouse(QWidget& widget, const QEvent::Type type, const QPoint& local, const Qt::MouseButton button,
               const Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(local), QPointF(widget.mapToGlobal(local)), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

/// The same, at a place on the screen, which may lie outside the widget: the widget that was pressed hears the moves.
void sendMouseAt(QWidget& widget, const QEvent::Type type, const QPoint& global, const Qt::MouseButton button,
                 const Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(widget.mapFromGlobal(global)), QPointF(global), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

/// Releases the button over a task's window the dock library is dragging, where the library listens: the system's title
/// bar - an event outside the client area - where the library uses one, its own title bar elsewhere.
void releaseTheLibrarysDrag(const an::ui::observatory::TaskWindow& floating, const QPoint& global)
{
    QWidget* const frame = floating.window();
    if (frame == nullptr)
    {
        return;
    }
    QWidget* receiver = frame;
    QEvent::Type type = QEvent::NonClientAreaMouseButtonRelease;
    for (QWidget* titleBar : frame->findChildren<AN_TitleBar*>())
    {
        if (titleBar->isVisible())
        {
            receiver = titleBar;
            type = QEvent::MouseButtonRelease;
            break;
        }
    }
    QCursor::setPos(global);
    QMouseEvent release(type, QPointF(receiver->mapFromGlobal(global)), QPointF(global), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QCoreApplication::sendEvent(receiver, &release);
}

/// A rail of one task, shown inside a host window, whose drags a controller hears.
struct RailWithATask
{
    RailWithATask(Drag& drag, FakeSurface& surface, QWidget& host)
    {
        // A child of a window already shown: a window of its own would take the activation, which the suites' platform
        // does not hand back.
        rail.setParent(&host);
        an::ui::RailItem task;
        task.id = QStringLiteral("task.7");
        task.section = an::ui::RailSection::Task;
        task.text = QStringLiteral("DeepSNR - drag");
        task.glyphKey = QStringLiteral("tune");
        rail.setItems({task});
        rail.resize(rail.sizeHint());
        rail.move(0, 0);
        rail.show();
        rail.raise();
        settle();
        drag.followRail(rail, [](const QString& id) { return id == QLatin1String("task.7") ? kTask : TaskId{0}; });
        // The main window is where the rail is, as wide and as high as the fake's.
        const QPoint origin = rail.mapToGlobal(QPoint(0, 0));
        surface.frameRect = QRect(origin - QPoint(0, 40), QSize(1000, 700));
        surface.railRect = QRect(origin, QSize(rail.width(), 632));
        surface.centreRect = QRect(surface.railRect.topRight() + QPoint(1, 0), QSize(596, 632));
        surface.onEndRailDrag = [this]() { rail.endDrag(); };
    }

    [[nodiscard]] QPoint thumbnail() const
    {
        return rail.itemRect(QStringLiteral("task.7")).center();
    }

    an::ui::AnIconRail rail;
};

/// The cases in the Observatory window open a process over a view of their own, as a person does.
class TaskDragCase : public ObservatoryWindowCase
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

    /// The rail's item of a task.
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

    /// The window's region on the screen.
    [[nodiscard]] static QRect onScreen(const QWidget& widget)
    {
        return QRect(widget.mapToGlobal(QPoint(0, 0)), widget.size());
    }
};

class TaskDrag : public TaskDragCase
{
};

} // namespace

// ---- The decisions, with an injected cursor and geometry

// The cases that show no window of their own come first: the suites' platform hands the activation back to no
// window once another has had it, and each case after that waits for it.

// Released inside the main window, the drag changes nothing, wherever it is released; Esc neither.
TEST_F(TaskDrag, ReleaseInsideCancels)
{
    FakeSurface surface;
    Drag drag(surface);
    for (const QPoint release :
         {surface.overTheRail(), surface.overTheCentre(), surface.overTheColumn(), surface.overTheTopBar()})
    {
        drag.railDragStarted(kTask, surface.overTheRail());
        drag.railDragMoved(release);
        drag.railDragReleased(release);
        EXPECT_EQ(drag.phase(), Drag::Phase::Idle);
    }
    drag.railDragStarted(kTask, surface.overTheRail());
    drag.railDragMoved(surface.overTheCentre());
    drag.railDragCancelled();
    EXPECT_EQ(drag.phase(), Drag::Phase::Idle);
    EXPECT_TRUE(surface.floated.empty());
    EXPECT_TRUE(surface.attached.empty());
}

// A task's window dragged by its title bar and released over the rail or the centre attaches; the window's owner makes
// it the current task.
TEST_F(TaskDrag, AWindowEndingOverTheRailOrTheCentreAttaches)
{
    FakeSurface surface;
    Drag drag(surface);
    drag.windowDragStarted(kTask);
    drag.windowDragStarted(kTask);
    EXPECT_EQ(drag.phase(), Drag::Phase::Window) << "asked again before the drag starts, it is the same drag";
    drag.windowDragMoved(surface.overTheRail());
    drag.windowDragEnded(surface.overTheRail());
    ASSERT_EQ(surface.attached, std::vector<TaskId>{kTask});
    EXPECT_EQ(drag.phase(), Drag::Phase::Idle);
    EXPECT_EQ(drag.zone(), AttachZone::None) << "the overlay goes with the drag";

    drag.windowDragStarted(kTask);
    drag.windowDragMoved(surface.overTheCentre());
    drag.windowDragEnded(surface.overTheCentre());
    EXPECT_EQ(surface.attached, (std::vector<TaskId>{kTask, kTask}));
}

// Released over the top bar, the column, the status line or outside the main window, the window stays a window.
TEST_F(TaskDrag, AWindowEndingOverTheTopBarTheColumnOrOutsideIsLeft)
{
    FakeSurface surface;
    Drag drag(surface);
    const QPoint statusLine(surface.centreRect.center().x(), surface.frameRect.bottom() - 10);
    for (const QPoint release : {surface.overTheTopBar(), surface.overTheColumn(), statusLine, surface.outside()})
    {
        drag.windowDragStarted(kTask);
        drag.windowDragMoved(release);
        drag.windowDragEnded(release);
        EXPECT_EQ(drag.phase(), Drag::Phase::Idle);
    }
    EXPECT_TRUE(surface.attached.empty());
}

// The overlay covers only the zone under the cursor - the rail or the centre - and the rail marks the task's place
// while it does; over anything else nothing is covered.
TEST_F(TaskDrag, TheOverlayCoversOnlyTheZoneUnderTheCursor)
{
    FakeSurface surface;
    Drag drag(surface);
    drag.windowDragStarted(kTask);
    ASSERT_EQ(drag.zone(), AttachZone::None);

    drag.windowDragMoved(surface.overTheRail());
    EXPECT_EQ(drag.zone(), AttachZone::Rail);
    ASSERT_FALSE(surface.zones.empty());
    EXPECT_EQ(surface.zones.back(), std::make_pair(AttachZone::Rail, kTask));
    const std::size_t shownOnce = surface.zones.size();
    drag.windowDragMoved(surface.overTheRail() + QPoint(0, 10));
    EXPECT_EQ(surface.zones.size(), shownOnce) << "a move within the zone shows nothing again";

    drag.windowDragMoved(surface.overTheCentre());
    EXPECT_EQ(drag.zone(), AttachZone::Centre);
    EXPECT_EQ(surface.zones.back(), std::make_pair(AttachZone::Centre, kTask));

    for (const QPoint elsewhere : {surface.overTheTopBar(), surface.overTheColumn(), surface.outside()})
    {
        drag.windowDragMoved(surface.overTheCentre());
        drag.windowDragMoved(elsewhere);
        EXPECT_EQ(drag.zone(), AttachZone::None);
        EXPECT_EQ(surface.zones.back(), std::make_pair(AttachZone::None, TaskId{0}));
    }
    EXPECT_EQ(drag.zoneAt(surface.overTheRail()), AttachZone::Rail);
    EXPECT_EQ(drag.zoneAt(surface.overTheCentre()), AttachZone::Centre);
    EXPECT_EQ(drag.zoneAt(surface.overTheColumn()), AttachZone::None);
    drag.windowDragEnded(surface.outside());
}

// A drag that is over no zone any more - the pointer left the main window, or went over what nothing attaches to -
// takes the overlay away and goes on; a drag that ends with no release over a zone attaches nothing.
TEST_F(TaskDrag, ADragLeavingEveryZoneClearsTheOverlayAndACancelledOneAttachesNothing)
{
    FakeSurface surface;
    Drag drag(surface);
    drag.windowDragStarted(kTask);
    drag.windowDragMoved(surface.overTheCentre());
    ASSERT_EQ(drag.zone(), AttachZone::Centre);
    drag.windowDragLeft();
    EXPECT_EQ(drag.zone(), AttachZone::None);
    EXPECT_EQ(surface.zones.back(), std::make_pair(AttachZone::None, TaskId{0}));
    EXPECT_EQ(drag.phase(), Drag::Phase::Window) << "the drag goes on: the pointer may come back";

    drag.windowDragMoved(surface.overTheRail());
    ASSERT_EQ(drag.zone(), AttachZone::Rail);
    drag.windowDragCancelled();
    EXPECT_EQ(drag.phase(), Drag::Phase::Idle);
    EXPECT_EQ(drag.zone(), AttachZone::None);
    EXPECT_EQ(surface.zones.back(), std::make_pair(AttachZone::None, TaskId{0}));
    EXPECT_TRUE(surface.attached.empty());

    drag.windowDragLeft();
    drag.windowDragCancelled();
    EXPECT_EQ(drag.phase(), Drag::Phase::Idle) << "with no drag they change nothing";
}

// Where a window cannot follow the cursor the ghost goes on past the frame and the task floats where the button is
// released outside; released back inside, nothing happens.
TEST_F(TaskDrag, WithoutAWindowThatFollowsTheTaskFloatsAtRelease)
{
    FakeSurface surface;
    surface.follows = false;
    Drag drag(surface);
    drag.railDragStarted(kTask, surface.overTheRail());
    drag.railDragMoved(surface.outside());
    EXPECT_EQ(drag.phase(), Drag::Phase::Ghost) << "the ghost goes on";
    EXPECT_TRUE(surface.floated.empty());
    EXPECT_EQ(surface.railDragsEnded, 0);

    const QPoint release = surface.outside() + QPoint(30, 40);
    drag.railDragMoved(release);
    drag.railDragReleased(release);
    ASSERT_EQ(surface.floated.size(), 1u);
    EXPECT_EQ(surface.floated.front().at, release);
    EXPECT_FALSE(surface.floated.front().follow);
    EXPECT_EQ(drag.phase(), Drag::Phase::Idle);

    drag.railDragStarted(kTask, surface.overTheRail());
    drag.railDragMoved(surface.outside());
    drag.railDragMoved(surface.overTheCentre());
    drag.railDragReleased(surface.overTheCentre());
    EXPECT_EQ(surface.floated.size(), 1u) << "released inside";
}

// A window follows the cursor wherever the application can move it: not under Wayland.
TEST_F(TaskDrag, AWindowFollowsTheCursorUnlessThePlatformIsWayland)
{
    EXPECT_TRUE(an::ui::observatory::windowFollowsTheCursor(QStringLiteral("windows")));
    EXPECT_TRUE(an::ui::observatory::windowFollowsTheCursor(QStringLiteral("xcb")));
    EXPECT_TRUE(an::ui::observatory::windowFollowsTheCursor(QStringLiteral("offscreen")));
    EXPECT_FALSE(an::ui::observatory::windowFollowsTheCursor(QStringLiteral("wayland")));
    EXPECT_FALSE(an::ui::observatory::windowFollowsTheCursor(QStringLiteral("wayland-egl")));
}

// A thumbnail pressed and moved less than the platform's drag distance is no drag: no ghost, nothing floated.
TEST_F(TaskDrag, UnderTheDragDistanceNothing)
{
    FakeSurface surface;
    Drag drag(surface);
    RailWithATask shown(drag, surface, window());
    const QPoint press = shown.thumbnail();
    sendMouse(shown.rail, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendMouse(shown.rail, QEvent::MouseMove, press + QPoint(QApplication::startDragDistance() - 1, 0), Qt::NoButton,
              Qt::LeftButton);

    EXPECT_EQ(drag.phase(), Drag::Phase::Idle);
    EXPECT_FALSE(shown.rail.isDragging());
    EXPECT_TRUE(surface.floated.empty());
    sendMouse(shown.rail, QEvent::MouseButtonRelease, press, Qt::LeftButton, Qt::NoButton);
    EXPECT_EQ(drag.phase(), Drag::Phase::Idle);
}

// Past the drag distance and inside the main window, the thumbnail's ghost follows the cursor and nothing else
// happens: the task does not float, no zone is covered.
TEST_F(TaskDrag, AGhostInsideTheWindow)
{
    FakeSurface surface;
    Drag drag(surface);
    RailWithATask shown(drag, surface, window());
    const QPoint press = shown.thumbnail();
    sendMouse(shown.rail, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendMouse(shown.rail, QEvent::MouseMove, press + QPoint(QApplication::startDragDistance() + 40, 30), Qt::NoButton,
              Qt::LeftButton);
    ASSERT_TRUE(shown.rail.isDragging());

    EXPECT_EQ(drag.phase(), Drag::Phase::Ghost);
    EXPECT_EQ(drag.task(), kTask);
    ASSERT_NE(shown.rail.dragGhost(), nullptr);
    EXPECT_TRUE(shown.rail.dragGhost()->isVisible());
    sendMouseAt(shown.rail, QEvent::MouseMove, surface.overTheCentre(), Qt::NoButton, Qt::LeftButton);
    EXPECT_EQ(drag.phase(), Drag::Phase::Ghost) << "anywhere inside the frame";
    EXPECT_TRUE(surface.floated.empty());
    EXPECT_EQ(drag.zone(), AttachZone::None);
    shown.rail.endDrag();
}

// The cursor leaving the main window's frame floats the task under it, and the press goes on as the dock library's
// drag of its window: the rail's ghost ends.
TEST_F(TaskDrag, CrossingTheFrameFloatsUnderTheCursor)
{
    FakeSurface surface;
    Drag drag(surface);
    RailWithATask shown(drag, surface, window());
    const QPoint press = shown.thumbnail();
    sendMouse(shown.rail, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendMouse(shown.rail, QEvent::MouseMove, press + QPoint(QApplication::startDragDistance() + 40, 30), Qt::NoButton,
              Qt::LeftButton);
    ASSERT_EQ(drag.phase(), Drag::Phase::Ghost);

    const QPoint outside = surface.outside();
    sendMouseAt(shown.rail, QEvent::MouseMove, outside, Qt::NoButton, Qt::LeftButton);

    ASSERT_EQ(surface.floated.size(), 1u);
    EXPECT_EQ(surface.floated.front().id, kTask);
    EXPECT_EQ(surface.floated.front().at, outside) << "under the cursor";
    EXPECT_TRUE(surface.floated.front().follow) << "the library's drag follows";
    EXPECT_EQ(surface.railDragsEnded, 1);
    EXPECT_FALSE(shown.rail.isDragging()) << "the ghost ends: the library has the drag";
    EXPECT_EQ(drag.phase(), Drag::Phase::Window);
    EXPECT_EQ(drag.task(), kTask);

    // The library ends its drag outside: the task stays a window.
    drag.windowDragEnded(outside);
    EXPECT_TRUE(surface.attached.empty());
    EXPECT_EQ(drag.phase(), Drag::Phase::Idle);

    // A library that does not take the drag leaves the task floating where it was put.
    surface.libraryTakesTheDrag = false;
    drag.railDragStarted(kTask, surface.overTheRail());
    drag.railDragMoved(outside);
    EXPECT_EQ(surface.floated.size(), 2u);
    EXPECT_EQ(drag.phase(), Drag::Phase::Idle);
}

// ---- In the Observatory window

// A thumbnail pulled out of the main window by the rail floats its task with the window's top under the cursor: the
// page leaves the centre, the rail's item dims, and the drag is the dock library's.
TEST_F(TaskDrag, InTheWindowAThumbnailPulledAcrossTheFrameFloatsItsTask)
{
    ImageViewControl* const view = openView(QStringLiteral("drag-pull"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kTargetProcess);
    ASSERT_NE(process, nullptr);
    const TaskId id = tasks().model().taskFor(process)->id;
    ASSERT_TRUE(window().isTaskPageShown());
    an::ui::AnIconRail& rail = *window().rail();
    const QPoint press = rail.itemRect(Window::railTaskId(id)).center();
    ASSERT_FALSE(press.isNull());

    sendMouse(rail, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
    sendMouse(rail, QEvent::MouseMove, press + QPoint(QApplication::startDragDistance() + 40, 30), Qt::NoButton,
              Qt::LeftButton);
    ASSERT_EQ(window().taskDrag().phase(), Drag::Phase::Ghost);
    EXPECT_TRUE(window().isTaskPageShown()) << "nothing moves while the cursor is inside";

    const QPoint outside(window().frameGeometry().right() + 150, window().frameGeometry().center().y());
    sendMouseAt(rail, QEvent::MouseMove, outside, Qt::NoButton, Qt::LeftButton);
    settle();

    an::ui::observatory::TaskWindow* const floating = window().taskWindow(id);
    ASSERT_NE(floating, nullptr);
    ASSERT_TRUE(floating->isOpen());
    ASSERT_NE(floating->window(), nullptr);
    const QRect frame = floating->window()->frameGeometry();
    EXPECT_TRUE(frame.contains(outside)) << "under the cursor";
    EXPECT_LE(outside.y() - floating->window()->geometry().top(), an::ui::observatory::TaskWindow::kGripOffset)
        << "held within its title";
    EXPECT_EQ(tasks().model().task(id)->presentation, TaskPresentation::Window);
    EXPECT_TRUE(railItem(id).floating);
    EXPECT_FALSE(rail.isDragging()) << "the library has the drag";
    EXPECT_FALSE(window().isTaskPageShown());

    // The button released: the library's drag ends, outside the main window, and the task stays a window.
    releaseTheLibrarysDrag(*floating, outside);
    settle();
    EXPECT_EQ(window().taskDrag().phase(), Drag::Phase::Idle);
    EXPECT_NE(window().taskWindow(id), nullptr);
}

// A task's window dragged over the main window covers the zone under the cursor with the overlay and marks its place in
// the rail; released over the centre it attaches, the current task with its page shown, and the overlay and the mark
// go.
TEST_F(TaskDrag, InTheWindowAWindowReleasedOverTheCentreAttachesAndIsSelected)
{
    ImageViewControl* const view = openView(QStringLiteral("drag-attach"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kTargetProcess);
    ASSERT_NE(process, nullptr);
    const TaskId id = tasks().model().taskFor(process)->id;
    ASSERT_TRUE(window().floatTask(id));
    settle();
    an::ui::observatory::TaskWindow* const floating = window().taskWindow(id);
    ASSERT_NE(floating, nullptr);

    // The library says a drag of the task's window starts.
    ASSERT_TRUE(an::ui::shell::DockHooks::allowsDrag(
        an::ui::shell::DockDrag{.mainWindow = nullptr, .dockWidget = floating->dockWidget()}));
    ASSERT_EQ(window().taskDrag().phase(), Drag::Phase::Window);
    ASSERT_EQ(window().taskDrag().task(), id);

    QWidget* const overlay = window().attachZoneOverlay();
    ASSERT_NE(overlay, nullptr);
    const QRect rail = onScreen(*window().rail());
    const QRect centre = onScreen(*window().centre());
    window().taskDrag().windowDragMoved(rail.center());
    EXPECT_TRUE(overlay->isVisible());
    EXPECT_EQ(onScreen(*overlay), rail) << "the overlay covers the rail alone";
    EXPECT_FALSE(window().rail()->insertionLineRect().isNull()) << "the rail marks the task's place";
    EXPECT_EQ(window().rail()->insertionMark(), Window::railTaskId(id));

    window().taskDrag().windowDragMoved(centre.center());
    EXPECT_TRUE(overlay->isVisible());
    EXPECT_EQ(onScreen(*overlay), centre) << "the overlay covers the centre alone";

    window().taskDrag().windowDragMoved(QPoint(centre.right() + 400, centre.center().y()));
    EXPECT_FALSE(overlay->isVisible()) << "over nothing it can attach to";
    EXPECT_TRUE(window().rail()->insertionLineRect().isNull());

    window().taskDrag().windowDragMoved(centre.center());
    window().taskDrag().windowDragEnded(centre.center());
    settle();
    EXPECT_FALSE(overlay->isVisible());
    EXPECT_TRUE(window().rail()->insertionLineRect().isNull());
    EXPECT_TRUE(window().isTaskPageShown());
    EXPECT_EQ(window().shownTask(), id);
    EXPECT_EQ(tasks().model().current(), id);
    EXPECT_EQ(tasks().model().task(id)->presentation, TaskPresentation::Page);
    EXPECT_FALSE(railItem(id).floating);
    EXPECT_TRUE(railItem(id).selected);
    EXPECT_EQ(window().taskWindow(id), nullptr);
}

// ---- Where the compositor alone moves a window (Wayland)

// There the dock library drags a task's window as a QDrag: the window stays where it is, and the drag's own events -
// which reach the widgets of the main window under the pointer - are all that says where it stands. The cases make the
// window answer as it does under Wayland and send those events as a compositor's drag would.
class WaylandDrag : public TaskDragCase
{
protected:
    void SetUp() override
    {
        TaskDragCase::SetUp();
        window().setDragPlatform(QStringLiteral("wayland"));
    }

    void TearDown() override
    {
        window().setDragPlatform(QString());
        TaskDragCase::TearDown();
    }

    /// A task of a process over a view, floating in its window, and the dock library's drag of the window begun.
    TaskId floatAndBeginTheDrag()
    {
        ImageViewControl* const view = openView(QStringLiteral("wayland-drag"));
        EXPECT_NE(view, nullptr);
        TopLevelProcessBaseControl* const process = launch(kTargetProcess);
        EXPECT_NE(process, nullptr);
        const TaskId id = process != nullptr ? tasks().model().taskFor(process)->id : TaskId{0};
        EXPECT_TRUE(window().floatTask(id));
        settle();
        an::ui::observatory::TaskWindow* const floating = window().taskWindow(id);
        EXPECT_NE(floating, nullptr);
        if (floating != nullptr)
        {
            EXPECT_TRUE(an::ui::shell::DockHooks::allowsDrag(
                an::ui::shell::DockDrag{.mainWindow = nullptr, .dockWidget = floating->dockWidget()}));
        }
        EXPECT_EQ(window().taskDrag().phase(), Drag::Phase::Window);
        return id;
    }

    /// The dock library reports its drag over, as it does once the QDrag it executes returns.
    static void theLibraryEndsItsDrag(const TaskId id)
    {
        an::ui::observatory::TaskWindow* const floating = window().taskWindow(id);
        ASSERT_NE(floating, nullptr);
        an::ui::shell::DockHooks::tellDragEnded(
            an::ui::shell::DockDrag{.mainWindow = nullptr, .dockWidget = floating->dockWidget()});
        settle();
    }

    /// A drag's event of a kind, sent to a widget at a place on the screen. Whether the widget took it.
    static bool dragEvent(QWidget& widget, const QEvent::Type type, const QPoint& global)
    {
        QMimeData mime;
        const QPoint local = widget.mapFromGlobal(global);
        switch (type)
        {
        case QEvent::DragEnter: {
            QDragEnterEvent event(local, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
            event.ignore();
            QApplication::sendEvent(&widget, &event);
            return event.isAccepted();
        }
        case QEvent::DragMove: {
            QDragMoveEvent event(local, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
            event.ignore();
            QApplication::sendEvent(&widget, &event);
            return event.isAccepted();
        }
        case QEvent::Drop: {
            QDropEvent event(QPointF(local), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
            event.ignore();
            QApplication::sendEvent(&widget, &event);
            return event.isAccepted();
        }
        default: {
            QDragLeaveEvent event;
            QApplication::sendEvent(&widget, &event);
            return true;
        }
        }
    }

    [[nodiscard]] static QPoint overTheTopBar()
    {
        const QRect frame = onScreen(window());
        return QPoint(frame.center().x(), frame.top() + 20);
    }
};

// The pointer over the rail or the centre covers that zone with the overlay, marks the task's place in the rail and
// takes the drop; over the top bar nothing is covered and the drop is not taken. A drop over a zone attaches the task
// once the library has ended its drag: the task is the current one with its page shown.
TEST_F(WaylandDrag, OverTheRailOrTheCentreTheOverlayShowsAndADropAttaches)
{
    const TaskId id = floatAndBeginTheDrag();
    QWidget* const overlay = window().attachZoneOverlay();
    ASSERT_NE(overlay, nullptr);
    QWidget& rail = *window().rail();
    QWidget& centre = *window().centre();

    EXPECT_TRUE(dragEvent(rail, QEvent::DragEnter, onScreen(rail).center())) << "a drop over the rail is taken";
    EXPECT_TRUE(overlay->isVisible());
    EXPECT_EQ(onScreen(*overlay), onScreen(rail)) << "the overlay covers the rail alone";
    EXPECT_EQ(window().rail()->insertionMark(), Window::railTaskId(id)) << "the rail marks the task's place";

    EXPECT_TRUE(dragEvent(centre, QEvent::DragMove, onScreen(centre).center()));
    EXPECT_EQ(onScreen(*overlay), onScreen(centre)) << "the overlay covers the centre alone";
    EXPECT_EQ(window().rail()->insertionMark(), Window::railTaskId(id)) << "the rail marks the place over either zone";

    EXPECT_FALSE(dragEvent(window(), QEvent::DragMove, overTheTopBar())) << "nothing attaches over the top bar";
    EXPECT_FALSE(overlay->isVisible());
    EXPECT_TRUE(window().rail()->insertionLineRect().isNull());

    EXPECT_TRUE(dragEvent(centre, QEvent::DragMove, onScreen(centre).center()));
    // Ignored, so the library ends its drag as one not dropped on its own areas, and tells so.
    EXPECT_FALSE(dragEvent(centre, QEvent::Drop, onScreen(centre).center()));
    EXPECT_NE(window().taskWindow(id), nullptr) << "the task attaches when the library has ended its drag";

    theLibraryEndsItsDrag(id);
    EXPECT_FALSE(overlay->isVisible());
    EXPECT_TRUE(window().rail()->insertionLineRect().isNull());
    EXPECT_EQ(window().taskDrag().phase(), Drag::Phase::Idle);
    EXPECT_EQ(window().taskWindow(id), nullptr);
    EXPECT_TRUE(window().isTaskPageShown());
    EXPECT_EQ(window().shownTask(), id);
    EXPECT_EQ(tasks().model().current(), id);
    EXPECT_EQ(tasks().model().task(id)->presentation, TaskPresentation::Page);
}

// A drag the pointer leaves, or one the person cancels, ends with no drop: the overlay goes, the window stays a window.
TEST_F(WaylandDrag, ADragThatEndsWithNoDropLeavesTheTaskAWindow)
{
    const TaskId id = floatAndBeginTheDrag();
    QWidget* const overlay = window().attachZoneOverlay();
    ASSERT_NE(overlay, nullptr);
    QWidget& rail = *window().rail();

    EXPECT_TRUE(dragEvent(rail, QEvent::DragEnter, onScreen(rail).center()));
    ASSERT_TRUE(overlay->isVisible());
    (void)dragEvent(rail, QEvent::DragLeave, onScreen(rail).center());
    settle();
    EXPECT_FALSE(overlay->isVisible()) << "the pointer left the main window";
    EXPECT_TRUE(window().rail()->insertionLineRect().isNull());

    theLibraryEndsItsDrag(id);
    EXPECT_EQ(window().taskDrag().phase(), Drag::Phase::Idle);
    ASSERT_NE(window().taskWindow(id), nullptr) << "nothing was dropped on a zone";
    EXPECT_FALSE(window().isTaskPageShown());
    EXPECT_EQ(tasks().model().task(id)->presentation, TaskPresentation::Window);
}

// Moving from one widget to another inside the zone sends a leave and an enter at once: the overlay does not go and
// come back.
TEST_F(WaylandDrag, MovingBetweenWidgetsOfAZoneDoesNotFlickerTheOverlay)
{
    const TaskId id = floatAndBeginTheDrag();
    QWidget* const overlay = window().attachZoneOverlay();
    ASSERT_NE(overlay, nullptr);
    QWidget& centre = *window().centre();
    EXPECT_TRUE(dragEvent(centre, QEvent::DragEnter, onScreen(centre).center()));
    ASSERT_TRUE(overlay->isVisible());
    int hides = 0;
    struct Count final : QObject
    {
        int* hides = nullptr;
        bool eventFilter(QObject*, QEvent* event) override
        {
            if (event->type() == QEvent::Hide)
            {
                ++*hides;
            }
            return false;
        }
    } count;
    count.hides = &hides;
    overlay->installEventFilter(&count);
    (void)dragEvent(centre, QEvent::DragLeave, onScreen(centre).center());
    EXPECT_TRUE(dragEvent(centre, QEvent::DragEnter, onScreen(centre).center() + QPoint(5, 5)));
    settle();
    EXPECT_EQ(hides, 0);
    EXPECT_TRUE(overlay->isVisible());
    overlay->removeEventFilter(&count);
    theLibraryEndsItsDrag(id);
}

// Where the library moves the window itself (every platform but Wayland) the drag's events are not read: the window
// follows the cursor and the library's own messages say where it stands.
TEST_F(WaylandDrag, WhereAWindowFollowsTheCursorTheDragEventsAreNotRead)
{
    window().setDragPlatform(QStringLiteral("xcb"));
    const TaskId id = floatAndBeginTheDrag();
    QWidget* const overlay = window().attachZoneOverlay();
    ASSERT_NE(overlay, nullptr);
    QWidget& rail = *window().rail();
    EXPECT_FALSE(dragEvent(rail, QEvent::DragEnter, onScreen(rail).center()));
    EXPECT_FALSE(overlay->isVisible());
    EXPECT_EQ(window().taskDrag().phase(), Drag::Phase::Window);
    window().taskDrag().windowDragEnded(QPoint(-5000, -5000));
    settle();
    EXPECT_NE(window().taskWindow(id), nullptr);
}

// With no task's window being dragged, a drag over the main window is none of the window's task drag: nothing is
// covered.
TEST_F(WaylandDrag, WithNoTaskWindowDraggedADragOverTheWindowCoversNothing)
{
    QWidget& rail = *window().rail();
    QWidget* const overlay = window().attachZoneOverlay();
    ASSERT_NE(overlay, nullptr);
    ASSERT_EQ(window().taskDrag().phase(), Drag::Phase::Idle);
    (void)dragEvent(rail, QEvent::DragEnter, onScreen(rail).center());
    (void)dragEvent(rail, QEvent::DragMove, onScreen(rail).center());
    EXPECT_FALSE(overlay->isVisible());
    EXPECT_TRUE(window().rail()->insertionLineRect().isNull());
}

// ---- A trial with a real mouse

// A trial on a platform with a screen: a task pulled out of the main window by its rail thumbnail, followed by the
// cursor, and dragged back by its title bar over the centre. A person runs it - the variable's value is how many
// seconds the window stays open - or, on Windows, the value `auto` has the system's own cursor do the two drags. The
// suite leaves it out unless it is asked for, and reports what it saw.
TEST_F(TaskDrag, HandTrialPullOutAndDragBack)
{
    const QByteArray asked = qgetenv(kHandTrialVariable);
    if (asked.isEmpty())
    {
        GTEST_SKIP() << "a trial with a real mouse: set " << kHandTrialVariable
                     << " to a number of seconds (or, on Windows, to auto) and run this case under a platform with a "
                        "screen (QT_QPA_PLATFORM=windows, xcb or wayland)";
    }
    const QString platform = QGuiApplication::platformName();
    if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal"))
    {
        GTEST_SKIP() << "the trial needs a screen: the platform is " << platform.toStdString();
    }
    const bool automatic = asked == "auto";
#if !defined(Q_OS_WIN)
    if (automatic)
    {
        GTEST_SKIP() << "the system's cursor is driven on Windows only: give a number of seconds and drag by hand";
    }
#endif

    // The main window on the primary screen with room at its right, an image and a page over it.
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    window().showNormal();
    window().setGeometry(QRect(screen.left() + 40, screen.top() + 60, (std::max)(1024, screen.width() * 55 / 100),
                               (std::max)(640, screen.height() * 75 / 100)));
    window().raise();
    window().activateWindow();
    settle();
    ImageViewControl* const view = openView(QStringLiteral("hand-trial"));
    ASSERT_NE(view, nullptr);
    TopLevelProcessBaseControl* const process = launch(kTargetProcess);
    ASSERT_NE(process, nullptr);
    const TaskId id = tasks().model().taskFor(process)->id;
    settle();

    // What the trial watches: the drag's phases, the window that floats and whether it stays under the cursor, the
    // overlay, and where the task ends.
    struct Seen
    {
        int pulls = 0;
        bool floatedUnderTheCursor = false;
        int moves = 0;
        int stray = 0;
        bool overlay = false;
        bool insertionLine = false;
        int attaches = 0;
        QPoint firstHold;
        bool holding = false;
    } seen;
    bool floatingBefore = false;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &window(), [&]() {
        const Drag::Phase phase = window().taskDrag().phase();
        an::ui::observatory::TaskWindow* const floating = window().taskWindow(id);
        const bool floatingNow = floating != nullptr && floating->window() != nullptr;
        if (floatingNow && !floatingBefore)
        {
            ++seen.pulls;
            seen.floatedUnderTheCursor = floating->window()->frameGeometry().contains(QCursor::pos());
            seen.holding = phase == Drag::Phase::Window;
            seen.firstHold = QCursor::pos() - floating->window()->frameGeometry().topLeft();
        }
        if (floatingNow && phase == Drag::Phase::Window)
        {
            const QPoint hold = QCursor::pos() - floating->window()->frameGeometry().topLeft();
            if (seen.holding)
            {
                ++seen.moves;
                seen.stray = (std::max)(seen.stray, (hold - seen.firstHold).manhattanLength());
            }
        }
        if (phase != Drag::Phase::Window)
        {
            seen.holding = false;
        }
        seen.overlay = seen.overlay || window().attachZoneOverlay()->isVisible();
        seen.insertionLine = seen.insertionLine || !window().rail()->insertionLineRect().isNull();
        if (!floatingNow && floatingBefore && tasks().model().task(id) != nullptr)
        {
            ++seen.attaches;
        }
        floatingBefore = floatingNow;
    });
    poll.start(10);

    QEventLoop loop;
    // Whether the system's cursor was kept from pressing: the trial's window was not in front, or not under it.
    std::atomic<bool> refused{false};
#if defined(Q_OS_WIN)
    std::thread driver;
    // The window of the trial, and the floating task's once there is one: a press goes only where one of them is. They,
    // and where the floating window's title bar is, are read on the GUI thread while the loop runs.
    const auto mainWindow = reinterpret_cast<HWND>(window().winId());
    std::atomic<HWND> floatingWindow{nullptr};
    std::atomic<int> titleX{0};
    std::atomic<int> titleY{0};
    // Physical pixels of the primary screen, which the system's cursor moves in.
    const qreal ratio = QGuiApplication::primaryScreen()->devicePixelRatio();
    const auto physical = [ratio](const QPoint& logical) {
        return QPoint(qRound(logical.x() * ratio), qRound(logical.y() * ratio));
    };
    if (automatic)
    {
        const QPoint thumbnail =
            physical(window().rail()->mapToGlobal(window().rail()->itemRect(Window::railTaskId(id)).center()));
        const QRect frame = window().frameGeometry();
        const QPoint outside =
            physical(QPoint(frame.right() + (screen.right() - frame.right()) / 2, frame.center().y()));
        const QPoint centre = physical(window().centre()->mapToGlobal(window().centre()->rect().center()));
        QObject::connect(&poll, &QTimer::timeout, &window(), [&]() {
            if (an::ui::observatory::TaskWindow* const floating = window().taskWindow(id);
                floating != nullptr && floating->window() != nullptr)
            {
                const QRect outer = floating->window()->frameGeometry();
                const QRect inner = floating->window()->geometry();
                const QPoint grip = physical(QPoint(inner.left() + 80, (outer.top() + inner.top()) / 2));
                titleX = grip.x();
                titleY = grip.y();
                floatingWindow = reinterpret_cast<HWND>(floating->window()->winId());
            }
        });
        driver = std::thread([&, thumbnail, outside, centre, mainWindow]() {
            // A press lands only on the trial's own window: a locked desktop, or another window in front, ends it.
            const auto under = [](const QPoint& at) {
                return GetAncestor(WindowFromPoint(POINT{at.x(), at.y()}), GA_ROOT);
            };
            const auto send = [](const QPoint& at, const DWORD flags) {
                const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
                const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
                const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
                INPUT input{};
                input.type = INPUT_MOUSE;
                input.mi.dx = static_cast<LONG>((at.x() - left) * 65535.0 / (width - 1));
                input.mi.dy = static_cast<LONG>((at.y() - top) * 65535.0 / (height - 1));
                input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | flags;
                SendInput(1, &input, sizeof(INPUT));
            };
            const auto glide = [&send](const QPoint& from, const QPoint& to, const int steps) {
                for (int step = 1; step <= steps; ++step)
                {
                    send(from + (to - from) * step / steps, 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(15));
                }
            };
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            // The first drag: the thumbnail out of the main window, on past its frame, and further.
            send(thumbnail, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (GetForegroundWindow() != mainWindow || under(thumbnail) != mainWindow)
            {
                refused = true;
                QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
                return;
            }
            send(thumbnail, MOUSEEVENTF_LEFTDOWN);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            glide(thumbnail, outside, 40);
            glide(outside, outside + QPoint(0, 120), 20);
            send(outside + QPoint(0, 120), MOUSEEVENTF_LEFTUP);
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            // The second: the task's window by its title bar, over the main window's centre.
            const QPoint grip(titleX.load(), titleY.load());
            send(grip, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if ((grip.x() != 0 || grip.y() != 0) && floatingWindow.load() != nullptr &&
                under(grip) == floatingWindow.load())
            {
                send(grip, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                send(grip, MOUSEEVENTF_LEFTDOWN);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                glide(grip, centre, 40);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                send(centre, MOUSEEVENTF_LEFTUP);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
        });
    }
#endif
    if (!automatic)
    {
        std::cout
            << "Hand trial: pull the task's thumbnail in the rail out past the window's edge, keep the button down "
               "and move: the task's window should follow. Release it, then drag it by its title bar back over "
               "the centre and release: it should attach. Close the main window when you are done."
            << std::endl;
        const int seconds = asked.toInt();
        if (seconds > 0)
        {
            QTimer::singleShot(seconds * 1000, &loop, &QEventLoop::quit);
        }
        QTimer closed;
        QObject::connect(&closed, &QTimer::timeout, &loop, [&]() {
            if (!window().isVisible())
            {
                loop.quit();
            }
        });
        closed.start(100);
        loop.exec();
    }
    else
    {
        loop.exec();
    }
#if defined(Q_OS_WIN)
    if (driver.joinable())
    {
        driver.join();
    }
#endif
    poll.stop();
    settle();
    if (refused)
    {
        GTEST_SKIP() << "the trial's window was not in front of the desktop - a locked or busy one - so the system's "
                        "cursor pressed nothing";
    }

    const an::ui::shell::Task* const task = tasks().model().task(id);
    const bool attached =
        task != nullptr && task->presentation == TaskPresentation::Page && window().taskWindow(id) == nullptr;
    const QString report =
        QStringLiteral("task drag trial on %1 (%2): pulled out %3 time(s), under the cursor: %4; the window moved %5 "
                       "time(s) while dragged, its corner straying %6 px at most from where the cursor held it; the "
                       "attach overlay shown: %7, the rail's mark shown: %8; attached by a drag: %9 time(s); ends %10")
            .arg(platform, automatic ? QStringLiteral("the system's cursor driven") : QStringLiteral("by hand"))
            .arg(seen.pulls)
            .arg(seen.floatedUnderTheCursor ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(seen.moves)
            .arg(seen.stray)
            .arg(seen.overlay ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(seen.insertionLine ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(seen.attaches)
            .arg(attached ? QStringLiteral("attached, the current task: ") +
                                (tasks().model().current() == id ? QStringLiteral("yes") : QStringLiteral("no"))
                          : QStringLiteral("floating"));
    std::cout << report.toStdString() << std::endl;
    RecordProperty("hand_trial", report.toStdString());
    if (automatic)
    {
        EXPECT_GE(seen.pulls, 1);
        EXPECT_TRUE(seen.floatedUnderTheCursor);
        EXPECT_TRUE(seen.overlay);
        EXPECT_TRUE(attached);
    }
}

} // namespace an::tests::observatory
