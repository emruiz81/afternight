// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A drag started by code: what a task dragged out of the main window by its thumbnail needs. The library starts one
// on a floating dock widget, it is the drag a user's press starts - the same hooks, the same indicators, the same
// drop - and it ends when a release is delivered.

#include "DockingTestSupport.h"

#include <QCoreApplication>
#include <QCursor>
#include <QEventLoop>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <kddockwidgets/core/Draggable_p.h>

#include <algorithm>
#include <iostream>

namespace an::tests::docking
{

namespace
{

struct DragState
{
    bool refuse = false;
    int asked = 0;
    int ended = 0;
    QString dragged;
};

DragState g_drag;

bool noteADragStarting(KDDockWidgets::Core::Draggable* draggable)
{
    ++g_drag.asked;
    const auto* dockWidget = draggable->singleDockWidget();
    g_drag.dragged = dockWidget ? dockWidget->uniqueName() : QString();
    return !g_drag.refuse;
}

void noteADragEnding()
{
    ++g_drag.ended;
}

// The variable that tells the case it is the process started to run under the native platform.
constexpr char kNativeChildVariable[] = "AFTERNIGHT_TEST_DOCKING_NATIVE_CHILD";

// The variable that asks for the trial a person runs. Its value is how many seconds the trial's window stays open;
// with none it stays until it is closed.
constexpr char kHandTrialVariable[] = "AFTERNIGHT_TEST_DOCKING_HAND_TRIAL";

/// Watches a dragged window move: how often it moved, and how far its corner strayed from where the cursor held it.
class MoveWatcher : public QObject
{
public:
    using QObject::QObject;

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Move)
        {
            if (const auto* window = qobject_cast<QWidget*>(watched))
            {
                const QPoint hold = QCursor::pos() - window->frameGeometry().topLeft();
                if (m_moves == 0)
                {
                    m_firstHold = hold;
                }
                ++m_moves;
                m_stray = (std::max)(m_stray, (hold - m_firstHold).manhattanLength());
            }
        }
        return QObject::eventFilter(watched, event);
    }

    [[nodiscard]] int moves() const
    {
        return m_moves;
    }
    [[nodiscard]] int stray() const
    {
        return m_stray;
    }

private:
    QPoint m_firstHold;
    int m_moves = 0;
    int m_stray = 0;
};

} // namespace

class ProgrammaticDrag : public DockingTest
{
protected:
    void SetUp() override
    {
        DockingTest::SetUp();
        g_drag = DragState();
        KDDockWidgets::Config::self().setDragAboutToStartFunc(noteADragStarting);
        KDDockWidgets::Config::self().setDragEndedFunc(noteADragEnding);
    }

    void TearDown() override
    {
        g_drag = DragState();
        DockingTest::TearDown();
    }
};

TEST_F(ProgrammaticDrag, StartDraggingPutsAFloatingDockWidgetInADragThatCodeEnds)
{
    auto* view = makeView(QStringLiteral("view"));
    floatAt(view, QRect(520, 40, 240, 200));
    ASSERT_DOUBLE_EQ(windowOf(view)->windowOpacity(), 1.0);

    QCursor::setPos(view->mapToGlobal(view->rect().center()));
    ASSERT_TRUE(view->dockWidget()->startDragging());

    EXPECT_EQ(g_drag.asked, 1);
    EXPECT_EQ(g_drag.dragged, QStringLiteral("view"));
    EXPECT_EQ(g_drag.ended, 0);
    EXPECT_NEAR(windowOf(view)->windowOpacity(), 0.7, 0.01) << "the dragged window is drawn as one being dragged";
    EXPECT_FALSE(view->dockWidget()->startDragging()) << "one drag at a time";

    auto drag = SimulatedDrag::joining(view);
    drag.release();

    EXPECT_EQ(g_drag.ended, 1);
    EXPECT_DOUBLE_EQ(windowOf(view)->windowOpacity(), 1.0);
    EXPECT_TRUE(view->isFloating());
}

TEST_F(ProgrammaticDrag, ADragStartedByCodeShowsIndicatorsAndDropsWhereItIsReleased)
{
    const auto workspaces = makeWorkspaceStack();
    auto* docked = makeView(QStringLiteral("docked"));
    auto* view = makeView(QStringLiteral("view"));
    workspaces.first->addDockWidget(docked, KDDockWidgets::Location_OnLeft);
    floatAt(view, QRect(520, 40, 240, 200));

    QCursor::setPos(view->mapToGlobal(view->rect().center()));
    ASSERT_TRUE(view->dockWidget()->startDragging());

    auto drag = SimulatedDrag::joining(view);
    const QPoint overDocked = docked->mapToGlobal(docked->rect().center());
    ASSERT_TRUE(drag.moveOnto(indicatorsOf(workspaces.first), overDocked, KDDockWidgets::DropLocation_Right));
    EXPECT_EQ(indicatorWindowsOnScreen(), 1);
    drag.release();

    EXPECT_EQ(g_drag.ended, 1);
    EXPECT_FALSE(view->isFloating());
    EXPECT_EQ(dockedIn(workspaces.first), QStringList({QStringLiteral("docked"), QStringLiteral("view")}));
}

TEST_F(ProgrammaticDrag, ADragStartedByCodeIsRefusedByTheHook)
{
    auto* view = makeView(QStringLiteral("view"));
    floatAt(view, QRect(520, 40, 240, 200));
    g_drag.refuse = true;

    QCursor::setPos(view->mapToGlobal(view->rect().center()));
    EXPECT_FALSE(view->dockWidget()->startDragging());

    EXPECT_EQ(g_drag.asked, 1);
    EXPECT_DOUBLE_EQ(windowOf(view)->windowOpacity(), 1.0);

    // The refused start leaves the library waiting for a release, as a refused press does; the release ends it.
    auto drag = SimulatedDrag::joining(view);
    drag.release();
    EXPECT_EQ(g_drag.ended, 0);

    g_drag.refuse = false;
    EXPECT_TRUE(view->dockWidget()->startDragging()) << "a refused start does not stop the next one";
    SimulatedDrag::joining(view).release();
    EXPECT_EQ(g_drag.ended, 1);
}

TEST_F(ProgrammaticDrag, ATaskWindowIsDraggedByCodeToo)
{
    auto* task = makeTask(QStringLiteral("task"));
    floatAt(task, QRect(520, 40, 300, 240));

    QCursor::setPos(task->mapToGlobal(task->rect().center()));
    ASSERT_TRUE(task->dockWidget()->startDragging());
    EXPECT_EQ(g_drag.dragged, QStringLiteral("task"));
    EXPECT_NEAR(windowOf(task)->windowOpacity(), 0.7, 0.01);

    auto drag = SimulatedDrag::joining(task);
    drag.moveTo(QPoint(300, 600));
    drag.release();

    EXPECT_EQ(g_drag.ended, 1);
    EXPECT_TRUE(task->isFloating());
}

TEST_F(ProgrammaticDrag, ATaskIsPulledOutOfItsWindowByAPressedThumbnail)
{
    const auto workspaces = makeWorkspaceStack();
    auto* task = makeTask(QStringLiteral("task"));
    auto* thumbnail = new TaskThumbnail(task, workspaces.window);
    thumbnail->setGeometry(8, 8, 48, 48);
    thumbnail->show();
    thumbnail->raise();
    settle();

    // Pressed, and moved inside the window: the task stays where it is.
    const QPoint onTheThumbnail = thumbnail->mapToGlobal(thumbnail->rect().center());
    deliverMouse(thumbnail, QEvent::MouseButtonPress, onTheThumbnail);
    deliverMouse(thumbnail, QEvent::MouseMove, onTheThumbnail + QPoint(120, 90));
    EXPECT_FALSE(task->isOpen());
    EXPECT_EQ(g_drag.asked, 0);

    // Moved past the window's frame: the task is a window under the cursor, and the library is dragging it.
    const QPoint outside(workspaces.window->frameGeometry().right() + 150, 300);
    deliverMouse(thumbnail, QEvent::MouseMove, outside);

    ASSERT_TRUE(task->isOpen());
    ASSERT_TRUE(task->isFloating());
    EXPECT_EQ(thumbnail->pulls(), 1);
    EXPECT_TRUE(thumbnail->dragStarted());
    EXPECT_TRUE(windowOf(task)->frameGeometry().contains(outside));
    EXPECT_EQ(g_drag.asked, 1);
    EXPECT_EQ(g_drag.dragged, QStringLiteral("task"));
    EXPECT_NEAR(windowOf(task)->windowOpacity(), 0.7, 0.01);
    EXPECT_TRUE(windowOf(task)->findChildren<AN_TabBar*>().isEmpty() ||
                !windowOf(task)->findChildren<AN_TabBar*>().constFirst()->isVisible());

    // The press goes on as the drag. Brought back over the workspaces, the task is offered nothing there.
    auto drag = SimulatedDrag::joining(task);
    drag.moveTo(workspaces.stack->mapToGlobal(workspaces.stack->rect().center()));
    EXPECT_EQ(indicatorWindowsOnScreen(), 0);
    drag.release();

    EXPECT_EQ(g_drag.ended, 1);
    EXPECT_TRUE(task->isFloating());
    EXPECT_DOUBLE_EQ(windowOf(task)->windowOpacity(), 1.0);
    EXPECT_EQ(thumbnail->releases(), 0)
        << "the release ends the drag where the library listens: the widget that was pressed is not told, and clears "
           "its pressed state when the drag ends";
}

// A trial for a person, on a platform with a screen: whether a task pulled out of its window by its thumbnail
// follows the cursor until the button is released. No case can press a real button, so the suite leaves this one
// out unless it is asked for, and reports what it saw for the person to read.
TEST_F(ProgrammaticDrag, HandTrialATaskPulledOutByItsThumbnailFollowsTheCursor)
{
    if (!qEnvironmentVariableIsSet(kHandTrialVariable))
    {
        GTEST_SKIP() << "a trial for a person: set " << kHandTrialVariable
                     << " and run this case under a platform with a screen (-platform windows, xcb or wayland)";
    }
    const QString platform = QGuiApplication::platformName();
    if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal"))
    {
        GTEST_SKIP() << "the trial needs a screen: the platform is " << platform.toStdString();
    }

    auto* window = own(new QWidget);
    window->setWindowTitle(QStringLiteral("Pull the task out of this window by its thumbnail"));
    auto* layout = new QVBoxLayout(window);
    auto* instructions = new QLabel(
        QStringLiteral("Press the square, drag it past this window's edge and keep the button down: a task window "
                       "should appear under the cursor and follow it.\nRelease it anywhere. Close its window to pull "
                       "it out again. Close this window when you are done."),
        window);
    instructions->setWordWrap(true);
    layout->addWidget(instructions);
    auto* row = new QHBoxLayout;
    layout->addLayout(row, 1);
    auto* task = makeTask(QStringLiteral("task"));
    auto* thumbnail = new TaskThumbnail(task, window);
    thumbnail->setFixedSize(56, 56);
    row->addWidget(thumbnail, 0, Qt::AlignTop);
    auto* workspace =
        own(new MainWindow(QStringLiteral("an.workspace.1"), KDDockWidgets::MainWindowOption_None, window));
    workspace->setWindowFlags(Qt::Widget);
    row->addWidget(workspace, 1);
    auto* view = makeView(QStringLiteral("image"));
    workspace->addDockWidget(view, KDDockWidgets::Location_OnLeft);
    window->resize(720, 480);
    window->show();

    // The task's window is watched from the moment it exists.
    MoveWatcher watcher;
    QPointer<QWidget> watched;
    QTimer poll;
    QEventLoop loop;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (task->isOpen() && windowOf(task) != watched)
        {
            watched = windowOf(task);
            watched->installEventFilter(&watcher);
        }
        if (!window->isVisible())
        {
            loop.quit();
        }
    });
    poll.start(15);
    const int seconds = qEnvironmentVariableIntValue(kHandTrialVariable);
    if (seconds > 0)
    {
        QTimer::singleShot(seconds * 1000, &loop, &QEventLoop::quit);
    }
    loop.exec();

    const QString report =
        QStringLiteral("hand trial on %1: pulled out %2 time(s); the library took the last pull as a drag: %3; drags "
                       "ended: %4; the task window moved %5 time(s) while it was watched, its corner straying %6 px "
                       "at most from where the cursor held it; releases the thumbnail heard: %7")
            .arg(platform)
            .arg(thumbnail->pulls())
            .arg(thumbnail->pulls() == 0    ? QStringLiteral("-")
                 : thumbnail->dragStarted() ? QStringLiteral("yes")
                                            : QStringLiteral("no"))
            .arg(g_drag.ended)
            .arg(watcher.moves())
            .arg(watcher.stray())
            .arg(thumbnail->releases());
    std::cout << report.toStdString() << std::endl;
    RecordProperty("hand_trial", report.toStdString());
}

// The same start and end with real windows and the system's title bar, which the offscreen platform has none of.
// Under any other platform the case starts this executable again under the native one and reads its verdict, so the
// suite holds the contract wherever it is run on Windows.
TEST_F(ProgrammaticDrag, StartsAndEndsUnderTheNativePlatformOnWindows)
{
#if !defined(Q_OS_WIN)
    GTEST_SKIP() << "the native title bar is the library's choice on Windows only";
#else
    if (!qEnvironmentVariableIsSet(kNativeChildVariable))
    {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("windows"));
        environment.insert(QString::fromLatin1(kNativeChildVariable), QStringLiteral("1"));

        QProcess child;
        child.setProcessEnvironment(environment);
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("--gtest_filter=ProgrammaticDrag.StartsAndEndsUnderTheNativePlatformOnWindows")});
        ASSERT_TRUE(child.waitForStarted(30000)) << child.errorString().toStdString();
        if (!child.waitForFinished(60000))
        {
            child.kill();
            child.waitForFinished(5000);
            FAIL() << "the run under the native platform did not end:\n" << child.readAll().toStdString();
        }
        EXPECT_EQ(child.exitStatus(), QProcess::NormalExit);
        EXPECT_EQ(child.exitCode(), 0) << child.readAll().toStdString();
        return;
    }

    ASSERT_EQ(QGuiApplication::platformName(), QStringLiteral("windows"));
    ASSERT_TRUE(floatingWindowsUseTheNativeTitleBar());

    auto* view = makeView(QStringLiteral("view"));
    // Off the screen: the window is real, and nobody has to see it.
    floatAt(view, QRect(-4000, -4000, 320, 240));
    ASSERT_TRUE(windowOf(view)->windowHandle() != nullptr);
    ASSERT_TRUE(windowOf(view)->isVisible());

    ASSERT_TRUE(view->dockWidget()->startDragging());

    EXPECT_EQ(g_drag.asked, 1);
    EXPECT_EQ(g_drag.dragged, QStringLiteral("view"));
    EXPECT_NEAR(windowOf(view)->windowOpacity(), 0.7, 0.01);

    // Ended by code, before the system is given the chance to move the window: the move it was asked to start waits
    // in the window's queue, and goes with the window.
    QMouseEvent release(QEvent::NonClientAreaMouseButtonRelease, QPointF(10, 10), QPointF(QCursor::pos()),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(windowOf(view), &release);

    EXPECT_EQ(g_drag.ended, 1);
    EXPECT_DOUBLE_EQ(windowOf(view)->windowOpacity(), 1.0);
    EXPECT_TRUE(view->isFloating());

    delete view;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
#endif
}

} // namespace an::tests::docking
