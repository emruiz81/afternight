// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The tasks a shell shows, with no widget: every open process, with the descriptor it was launched as, the image it
/// targets and where its run stands; and every session of the extension host, whose window is its host's own.

#include "AfterNightShellCoreExport.h"

#include "app/ExtensionSessionFeed.h"
#include "controls/ProcessPresentation.h"

#include <QImage>
#include <QPointer>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class ImageViewControl;
class TopLevelProcessBaseControl;

namespace an::ui::shell
{

class OpenProcessRegistry;

/// What a model tells a task by; never given to another task, and never zero.
using TaskId = std::uint64_t;

/// How a task is shown: as a page of the main window, as a window of its own, or by a program of its own.
enum class TaskPresentation : std::uint8_t
{
    Page,
    Window,
    External
};

/// An open process, as a shell shows it.
struct Task
{
    TaskId id = 0;

    /// The registry descriptor the process was launched as; empty for one opened outside a launch.
    std::string descriptorId;

    /// The name the process window was made with.
    QString processName;

    /// The descriptor's name, or the process's own for a process of no descriptor.
    QString displayName;

    /// The descriptor's category; empty for a process of no descriptor.
    QString category;

    /// The process's glyph, by the name of its file in the icons: the descriptor's icon, else its action's, else a
    /// generic one.
    QString iconKey;

    /// The process window; null for an External task.
    QPointer<TopLevelProcessBaseControl> process;

    /// The extension host session an External task shows; empty for any other task, and for an External task whose host
    /// failed.
    std::string sessionId;

    /// Where an External task's host stands; `Ready` for any other task.
    an::ui::extensions::ExtensionHostStanding host = an::ui::extensions::ExtensionHostStanding::Ready;

    /// Whether an External task's host shows its window in front when asked.
    bool raisesItsWindow = false;

    /// Whether an External task's session ended because its host failed: the task stays, its run failed with why, until
    /// it is closed.
    bool hostFailed = false;

    /// Whether the process works on one image, the one it was launched on: false for a process launched on no image,
    /// which chooses every image it reads in its controls and has no target, whatever image it reads.
    bool singleTarget = true;

    /// The image the process runs on; null for a process with no single target.
    QPointer<ImageViewControl> target;

    /// The target view's full name; for a process with no target that holds a job - an integration - the job's name;
    /// empty otherwise.
    QString targetName;

    ProcessSurface surface = ProcessSurface::Window;
    TaskPresentation presentation = TaskPresentation::Window;

    /// Where the process's run stands. A run that succeeded or failed keeps that until the task is shown again.
    ProcessRunState run;

    /// Whether the run ended - succeeded or failed - while the task was not the current one, and the task has not been
    /// shown since.
    bool unseenResult = false;

    /// The target's thumbnail, `ViewThumbnailService::kSide` square; null until one is rendered, and with no target.
    QImage thumbnail;
};

/// The list of the open processes as tasks, in the order they were opened.
///
/// A task is made when the process registry registers a process window and taken away when it forgets it; the model
/// takes in the windows already open when it is made. It reads the window's descriptor id - which a window made inside
/// a launch states as it registers - and the registry's descriptor for the names; the shell's presenter tells it the
/// rest: where a run stands and which image a process targets. The model implements nothing visual.
///
/// **External tasks.** Given the sessions of the extension host, the model makes an External task of each session when
/// it starts - the window it shows is its host's, in another process - named by its descriptor, and follows where it
/// stands: its host starting, ready or not responding, and the run of its window as the host tells it. A session that
/// ends as it should takes its task away; one whose host failed leaves it, failed with why, until it is closed. A
/// host's run holds no lock on its image, so an External task never blocks a view.
///
/// **The current task** is the one the person looks at: a page the main window shows, or a window that is active. A run
/// that ends - succeeded or failed - on a task that is not current leaves an unseen result, which making the task
/// current clears, the ended run with it.
///
/// Every change is told to the observers on the GUI thread, a change that changes nothing to none. Used on the GUI
/// thread.
class AFTERNIGHTSHELLCORE_API TaskModel final
{
public:
    /// What changed.
    enum class Change : std::uint8_t
    {
        Added,
        Removed,
        Changed,
        Current
    };

    using Observer = std::function<void(Change change, TaskId id)>;

    /// The glyph of a process whose descriptor and action name none.
    static constexpr char kGenericGlyph[] = "tune";

    /// Makes the model over the open processes, a task each of the windows open now; the registry outlives the model.
    explicit TaskModel(OpenProcessRegistry& processes);

    /// Makes the model over the open processes and the sessions of the extension host, a task each of the windows open
    /// now and of the sessions running now; the registry and the sessions outlive the model, and no sessions are
    /// followed when `sessions` is null.
    TaskModel(OpenProcessRegistry& processes, an::ui::extensions::IExtensionSessionFeed* sessions);
    ~TaskModel();

    TaskModel(const TaskModel&) = delete;
    TaskModel& operator=(const TaskModel&) = delete;
    TaskModel(TaskModel&&) = delete;
    TaskModel& operator=(TaskModel&&) = delete;

    /// Every task, in the order the processes were opened.
    [[nodiscard]] const std::vector<Task>& tasks() const noexcept;

    /// The task of an id; null for one the model does not hold.
    [[nodiscard]] const Task* task(TaskId id) const;

    /// The task of a process window; null for one the model does not hold.
    [[nodiscard]] const Task* taskFor(const TopLevelProcessBaseControl* process) const;

    /// The External task of an extension host session; null for a session the model holds none of.
    [[nodiscard]] const Task* taskForSession(std::string_view sessionId) const;

    /// The tasks that target a view, in the order they were opened.
    [[nodiscard]] std::vector<TaskId> tasksOn(const ImageViewControl* view) const;

    /// The task whose run is on its way over a view, holding it; null when none is. An External task's never is: its
    /// host holds no lock on the image.
    [[nodiscard]] const Task* runningTaskOn(const ImageViewControl* view) const;

    /// The current task; zero for none.
    [[nodiscard]] TaskId current() const noexcept;

    /// Makes a task the current one, or none for zero: its unseen result is seen, and its ended run is let go - but for
    /// an External task whose host failed, which stays failed until it is closed. False for an id the model does not
    /// hold.
    bool setCurrent(TaskId id);

    /// Asks the host of an External task to show its window in front. False for any other task, for one whose host
    /// does not raise its window and for one whose host failed.
    bool raiseExternal(TaskId id);

    /// Closes an External task: its session is ended as it should and the task goes with it, or the task of a host that
    /// failed is dismissed. False for any other task.
    bool closeExternal(TaskId id);

    // ---- What the shell's presenter tells it.

    /// A process's run moved on.
    void setRunState(const TopLevelProcessBaseControl& process, const ProcessRunState& state);

    /// A process targets another view, or none.
    void setTarget(const TopLevelProcessBaseControl& process, ImageViewControl* target);

    /// A view was renamed: the tasks that target it take its new name.
    void targetRenamed(const ImageViewControl* view);

    /// A process that targets no image says the job it holds, or that it holds none: its task is named by it. Nothing
    /// for a task that targets an image, which is named by that.
    void setJobName(const TopLevelProcessBaseControl& process, const QString& name);

    /// A task's target's thumbnail.
    void setThumbnail(TaskId id, const QImage& thumbnail);

    /// How a task is shown.
    void setPresentation(TaskId id, TaskPresentation presentation);

    /// Adds an observer, told of every change from now on; the token takes it out again.
    std::size_t subscribe(Observer observer);
    void unsubscribe(std::size_t token);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::shell
