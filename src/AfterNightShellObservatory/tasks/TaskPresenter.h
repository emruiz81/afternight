// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the Observatory shell shows of its processes: a task each in the rail, the notice over an image a run writes,
/// the glyph on the tab of a task's image and the pending step of its history.

#include "AfterNightShellObservatoryExport.h"

#include "tasks/TaskModel.h"

#include "components/AnHistoryStrip.h"
#include "controls/ProcessPresentation.h"

#include <QString>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>

class ImageViewControl;
class TopLevelProcessBaseControl;

namespace an::ui::shell
{
class ApplicationCore;
class ViewThumbnailService;
} // namespace an::ui::shell

namespace an::ui::observatory
{

/// The Observatory shell's process presenter. It takes no process into a page yet - every process is a window of its
/// own - and keeps the shell's tasks: the model of the open processes, told where each run stands and which image each
/// process targets, and the thumbnails of their targets.
///
/// **What it shows.** A view a run writes - its processing lock taken - carries the notice that names the running task
/// and how far it has come, with `Show`; the tab of a view a task is open on carries the task's glyph, locked during a
/// run, which shows the task; a view's history strip shows the task's pending step, "writing" during a run. The rail's
/// items are the window's, made from the model.
///
/// **Showing a task.** A task in a window is shown by raising and activating its window; the task whose window is
/// active is the current one. Used on the GUI thread.
class AFTERNIGHTSHELLOBSERVATORY_API TaskPresenter final : public an::ui::IProcessPresenter
{
public:
    /// What the presenter tells the window: a task's view - its notice, its glyph, its pending step - changed, or the
    /// tasks changed and the rail is made again.
    struct Hooks
    {
        std::function<void()> tasksChanged;
        std::function<void(ImageViewControl* view)> viewChanged;
    };

    explicit TaskPresenter(an::ui::shell::ApplicationCore& core);
    ~TaskPresenter() override;

    TaskPresenter(const TaskPresenter&) = delete;
    TaskPresenter& operator=(const TaskPresenter&) = delete;
    TaskPresenter(TaskPresenter&&) = delete;
    TaskPresenter& operator=(TaskPresenter&&) = delete;

    void setHooks(Hooks hooks);

    // ---- IProcessPresenter

    /// Takes no process: every process is a window until it is given a page.
    bool adopt(TopLevelProcessBaseControl& process) override;
    void runStateChanged(TopLevelProcessBaseControl& process, const an::ui::ProcessRunState& state) override;
    void targetChanged(TopLevelProcessBaseControl& process, ImageViewControl* target) override;

    // ---- The tasks

    [[nodiscard]] an::ui::shell::TaskModel& model() noexcept;
    [[nodiscard]] an::ui::shell::ViewThumbnailService& thumbnails() noexcept;

    /// Shows a task: raises and activates its window, which makes it the current task. False for a task the model does
    /// not hold or whose window is gone.
    bool show(an::ui::shell::TaskId id);

    /// Closes a task's process as its window's Close does; refused while its run is on its way.
    bool close(an::ui::shell::TaskId id);

    /// Whether a task's process may be closed now: not while its run is on its way.
    [[nodiscard]] bool canClose(an::ui::shell::TaskId id) const;

    /// The task that names a view: the one whose run writes it, else the first opened on it; null for none.
    [[nodiscard]] const an::ui::shell::Task* taskOn(const ImageViewControl* view) const;

    /// The pending step a view's history strip shows for the task open on it; nothing with none.
    [[nodiscard]] std::optional<an::ui::HistoryPendingStep> pendingStepOf(const ImageViewControl* view) const;

    /// The text of the notice a view carries while a run writes it: "Non Local Bayes is applying".
    [[nodiscard]] QString noticeTextOf(const ImageViewControl* view) const;

    /// Follows a view the shell shows: its processing lock, for the notice. A view followed already changes nothing.
    void watch(ImageViewControl& view);

    /// The window that shows a process is now the active one, or none is a process's: the current task follows.
    void activeWindowChanged(QWidget* window);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::observatory
