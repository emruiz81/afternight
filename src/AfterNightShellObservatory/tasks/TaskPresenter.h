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

/// The Observatory shell's process presenter. It takes a process whose descriptor states a page into a page of the
/// window (`Hooks::adoptPage`) and leaves a process whose descriptor states a window a window of its own; and it keeps
/// the shell's tasks: the model of the open processes, told where each run stands and which image each process targets,
/// and the thumbnails of their targets.
///
/// **What it shows.** A view a run writes - its processing lock taken - carries the notice that names the running task
/// and how far it has come, with `Show`; the tab of a view a task is open on carries the task's glyph, locked during a
/// run, which shows the task; a view's history strip shows the task's pending step, "writing" during a run. The rail's
/// items are the window's, made from the model.
///
/// **Showing a task.** A task in a page is shown by the window (`Hooks::showPage`); a task in a window by raising and
/// activating its window - its process's own, or the window its page floats in (`Hooks::showWindow`) - the task whose
/// window is active being the current one. Used on the GUI thread.
class AFTERNIGHTSHELLOBSERVATORY_API TaskPresenter final : public an::ui::IProcessPresenter
{
public:
    /// What the presenter tells the window: a task's view - its notice, its glyph, its pending step - changed, or the
    /// tasks changed and the rail is made again.
    struct Hooks
    {
        std::function<void()> tasksChanged;
        std::function<void(ImageViewControl* view)> viewChanged;
        /// Takes a process into a page of the window; false leaves it a window.
        std::function<bool(TopLevelProcessBaseControl& process)> adoptPage;
        /// Shows a task's page in the centre.
        std::function<void(an::ui::shell::TaskId id)> showPage;
        /// Raises and activates the window of a task whose page floats in a window of its own.
        std::function<void(an::ui::shell::TaskId id)> showWindow;
        /// A task's run moved on, from where it stood.
        std::function<void(an::ui::shell::TaskId id, const an::ui::ProcessRunState& before)> runChanged;
    };

    explicit TaskPresenter(an::ui::shell::ApplicationCore& core);
    ~TaskPresenter() override;

    TaskPresenter(const TaskPresenter&) = delete;
    TaskPresenter& operator=(const TaskPresenter&) = delete;
    TaskPresenter(TaskPresenter&&) = delete;
    TaskPresenter& operator=(TaskPresenter&&) = delete;

    void setHooks(Hooks hooks);

    // ---- IProcessPresenter

    /// Takes a process whose surface is a page - a preview, a mirror or a full page - into a page of the window, and
    /// shows it there; a process whose surface is a window is left one.
    bool adopt(TopLevelProcessBaseControl& process) override;
    void runStateChanged(TopLevelProcessBaseControl& process, const an::ui::ProcessRunState& state) override;
    void targetChanged(TopLevelProcessBaseControl& process, ImageViewControl* target) override;

    // ---- The tasks

    [[nodiscard]] an::ui::shell::TaskModel& model() noexcept;
    [[nodiscard]] an::ui::shell::ViewThumbnailService& thumbnails() noexcept;

    /// Shows a task: its page, or its window raised and activated; it is the current task then. False for a task the
    /// model does not hold or whose process is gone.
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

    /// The window that shows a process - the process's own, or the one its page floats in - is now the active one, or
    /// none is a process's: the current task follows.
    void activeWindowChanged(QWidget* window);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::observatory
