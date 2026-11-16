// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "tasks/TaskPresenter.h"

#include "core/ApplicationCore.h"
#include "services/OpenProcessRegistry.h"
#include "views/ViewThumbnailService.h"

#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"
#include "controls/KDDockWidgetsHelper.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "controls/ViewChrome.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QWidget>

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

namespace an::ui::observatory
{

namespace
{

[[nodiscard]] QString translated(const char* text)
{
    return QCoreApplication::translate("TaskPresenter", text);
}

} // namespace

struct TaskPresenter::Impl
{
    /// Hears one view the shell shows: its processing lock, its name and its going.
    class ViewWatch final : public ImageViewControlListener
    {
    public:
        ViewWatch(Impl& impl, ImageViewControl& view) : ImageViewControlListener(&view), m_impl(impl) {}

        void onProcessingLockChanged(ImageViewControl* view, bool /*locked*/) override
        {
            m_impl.refresh(view);
        }

        void onImageRenamed(ImageViewControl* view) override
        {
            m_impl.model.targetRenamed(view);
        }

        void onImageViewControlDestroyed(ImageViewControl* view) override
        {
            ImageViewControlListener::onImageViewControlDestroyed(view);
            m_impl.unwatch(view);
        }

    private:
        Impl& m_impl;
    };

    explicit Impl(an::ui::shell::ApplicationCore& application)
        : core(application), model(application.openProcesses()), thumbnails(application)
    {
    }

    an::ui::shell::ApplicationCore& core;
    an::ui::shell::TaskModel model;
    an::ui::shell::ViewThumbnailService thumbnails;
    Hooks hooks;
    std::size_t modelSubscription = 0;
    std::size_t thumbnailSubscription = 0;
    std::map<ImageViewControl*, std::unique_ptr<ViewWatch>> watched;
    /// The watches of views that went, destroyed once the event loop turns: a view tells its listeners it goes while
    /// one of them is still being told.
    std::vector<std::unique_ptr<ViewWatch>> retired;
    /// The view each task was last shown on, so a task that leaves a view clears what that view showed of it.
    std::map<an::ui::shell::TaskId, QPointer<ImageViewControl>> shownOn;
    QObject gate;

    void watch(ImageViewControl& view)
    {
        if (!watched.contains(&view))
        {
            watched.emplace(&view, std::make_unique<ViewWatch>(*this, view));
        }
    }

    void unwatch(ImageViewControl* view)
    {
        const auto found = watched.find(view);
        if (found == watched.end())
        {
            return;
        }
        found->second->setViewControl(nullptr);
        retired.push_back(std::move(found->second));
        watched.erase(found);
        QMetaObject::invokeMethod(&gate, [this]() { retired.clear(); }, Qt::QueuedConnection);
    }

    /// The tasks on a view, their thumbnails and what the view shows of them.
    void taskChanged(const an::ui::shell::TaskModel::Change change, const an::ui::shell::TaskId id)
    {
        std::set<ImageViewControl*> touched;
        if (const auto before = shownOn.find(id); before != shownOn.end())
        {
            if (!before->second.isNull())
            {
                touched.insert(before->second.data());
            }
            if (change == an::ui::shell::TaskModel::Change::Removed)
            {
                shownOn.erase(before);
            }
        }
        if (const an::ui::shell::Task* task = model.task(id))
        {
            ImageViewControl* target = task->target.data();
            shownOn[id] = target;
            if (target != nullptr)
            {
                touched.insert(target);
                watch(*target);
                if (!thumbnails.follows(target))
                {
                    thumbnails.follow(target);
                }
                else if (task->thumbnail.isNull())
                {
                    const QImage known = thumbnails.thumbnail(target);
                    if (!known.isNull())
                    {
                        model.setThumbnail(id, known);
                    }
                }
            }
        }
        // A view no task targets any more needs no thumbnail.
        for (ImageViewControl* view : touched)
        {
            if (model.tasksOn(view).empty())
            {
                thumbnails.forget(view);
            }
            refresh(view);
        }
        if (hooks.tasksChanged)
        {
            hooks.tasksChanged();
        }
    }

    void thumbnailPublished(ImageViewControl* view, const QImage& thumbnail)
    {
        for (const an::ui::shell::TaskId id : model.tasksOn(view))
        {
            model.setThumbnail(id, thumbnail);
        }
    }

    [[nodiscard]] const an::ui::shell::Task* taskOn(const ImageViewControl* view) const
    {
        if (const an::ui::shell::Task* running = model.runningTaskOn(view))
        {
            return running;
        }
        const std::vector<an::ui::shell::TaskId> on = model.tasksOn(view);
        return on.empty() ? nullptr : model.task(on.front());
    }

    [[nodiscard]] QString noticeText(const ImageViewControl* view) const
    {
        if (const an::ui::shell::Task* running = model.runningTaskOn(view))
        {
            return translated("%1 is %2")
                .arg(running->displayName, running->run.text.isEmpty() ? an::ui::applyingRunText() : running->run.text);
        }
        return translated("This image is being processed");
    }

    /// What a view shows of the tasks on it: the notice while a run writes it, the glyph on its tab.
    void refresh(ImageViewControl* view)
    {
        if (view == nullptr || !watched.contains(view))
        {
            return;
        }
        // A view is blocked while a run writes it: its processing lock, or a task's run on it - an edit of a still
        // image takes the image's writer, not the view's lock.
        const bool locked = view->isLockedForProcessing() || model.runningTaskOn(view) != nullptr;
        const an::ui::shell::Task* task = taskOn(view);
        if (locked)
        {
            an::ui::ProcessingNotice notice;
            notice.text = noticeText(view);
            if (const an::ui::shell::Task* running = model.runningTaskOn(view))
            {
                notice.progress = running->run.progress;
                notice.actionLabel = translated("Show");
                notice.action = [this, id = running->id]() { (void)show(id); };
            }
            view->setProcessingNotice(notice);
        }
        else if (view->hasProcessingNotice())
        {
            view->clearProcessingNotice();
        }
        if (KDDockWidgets::QtWidgets::DockWidget* dock = view->getDockWidget())
        {
            if (task == nullptr)
            {
                if (an::ui::tabAdornment(dock) != nullptr)
                {
                    an::ui::clearTabAdornment(dock);
                }
            }
            else
            {
                an::ui::TabAdornment adornment;
                adornment.iconKey = task->iconKey;
                adornment.locked = locked;
                adornment.toolTip =
                    task->run.running()
                        ? translated("%1 is %2 to this image: it is locked until the run ends. Click to go to the task")
                              .arg(task->displayName,
                                   task->run.text.isEmpty() ? an::ui::applyingRunText() : task->run.text)
                        : translated("%1 is open on this image. Click to go to the task").arg(task->displayName);
                adornment.activated = [this, id = task->id]() { (void)show(id); };
                an::ui::setTabAdornment(dock, std::move(adornment));
            }
        }
        if (hooks.viewChanged)
        {
            hooks.viewChanged(view);
        }
    }

    bool show(const an::ui::shell::TaskId id)
    {
        const an::ui::shell::Task* task = model.task(id);
        if (task == nullptr || task->process.isNull())
        {
            return false;
        }
        if (task->presentation == an::ui::shell::TaskPresentation::Page && hooks.showPage)
        {
            hooks.showPage(id);
            (void)model.setCurrent(id);
            return true;
        }
        TopLevelProcessBaseControl* process = task->process.data();
        if (!process->isWindow())
        {
            // A page floating in a window of its own: the window is raised.
            if (!hooks.showWindow)
            {
                return false;
            }
            hooks.showWindow(id);
            (void)model.setCurrent(id);
            return true;
        }
        if (process->isMinimized())
        {
            process->showNormal();
        }
        else if (!process->isVisible())
        {
            process->show();
        }
        process->raise();
        process->activateWindow();
        (void)model.setCurrent(id);
        return true;
    }
};

TaskPresenter::TaskPresenter(an::ui::shell::ApplicationCore& core) : m_impl(std::make_unique<Impl>(core))
{
    Impl& impl = *m_impl;
    impl.modelSubscription =
        impl.model.subscribe([this](const an::ui::shell::TaskModel::Change change, const an::ui::shell::TaskId id) {
            m_impl->taskChanged(change, id);
        });
    impl.thumbnailSubscription = impl.thumbnails.subscribe(
        [this](ImageViewControl* view, const QImage& thumbnail) { m_impl->thumbnailPublished(view, thumbnail); });
    // The processes open now are tasks already: their targets are followed as the model's changes would follow them.
    for (const an::ui::shell::Task& task : impl.model.tasks())
    {
        impl.taskChanged(an::ui::shell::TaskModel::Change::Added, task.id);
    }
}

TaskPresenter::~TaskPresenter()
{
    Impl& impl = *m_impl;
    impl.hooks = {};
    impl.model.unsubscribe(impl.modelSubscription);
    impl.thumbnails.unsubscribe(impl.thumbnailSubscription);
}

void TaskPresenter::setHooks(Hooks hooks)
{
    m_impl->hooks = std::move(hooks);
}

bool TaskPresenter::adopt(TopLevelProcessBaseControl& process)
{
    Impl& impl = *m_impl;
    const an::ui::ProcessSurface surface = process.processSurface();
    if (surface == an::ui::ProcessSurface::Window || !impl.hooks.adoptPage)
    {
        return false;
    }
    const an::ui::shell::Task* task = impl.model.taskFor(&process);
    if (task == nullptr)
    {
        return false;
    }
    const an::ui::shell::TaskId id = task->id;
    if (!impl.hooks.adoptPage(process))
    {
        return false;
    }
    impl.model.setPresentation(id, an::ui::shell::TaskPresentation::Page);
    (void)impl.show(id);
    return true;
}

void TaskPresenter::runStateChanged(TopLevelProcessBaseControl& process, const an::ui::ProcessRunState& state)
{
    Impl& impl = *m_impl;
    const an::ui::shell::Task* task = impl.model.taskFor(&process);
    const an::ui::ProcessRunState before = task != nullptr ? task->run : an::ui::ProcessRunState{};
    const an::ui::shell::TaskId id = task != nullptr ? task->id : 0u;
    impl.model.setRunState(process, state);
    if (id != 0u && impl.hooks.runChanged)
    {
        impl.hooks.runChanged(id, before);
    }
}

void TaskPresenter::targetChanged(TopLevelProcessBaseControl& process, ImageViewControl* target)
{
    m_impl->model.setTarget(process, target);
}

an::ui::shell::TaskModel& TaskPresenter::model() noexcept
{
    return m_impl->model;
}

an::ui::shell::ViewThumbnailService& TaskPresenter::thumbnails() noexcept
{
    return m_impl->thumbnails;
}

bool TaskPresenter::show(const an::ui::shell::TaskId id)
{
    return m_impl->show(id);
}

bool TaskPresenter::close(const an::ui::shell::TaskId id)
{
    if (!canClose(id))
    {
        return false;
    }
    const an::ui::shell::Task* task = m_impl->model.task(id);
    if (task == nullptr || task->process.isNull())
    {
        return false;
    }
    return task->process->close();
}

bool TaskPresenter::canClose(const an::ui::shell::TaskId id) const
{
    const an::ui::shell::Task* task = m_impl->model.task(id);
    return task != nullptr && !task->process.isNull() && !task->run.running();
}

const an::ui::shell::Task* TaskPresenter::taskOn(const ImageViewControl* view) const
{
    return m_impl->taskOn(view);
}

std::optional<an::ui::HistoryPendingStep> TaskPresenter::pendingStepOf(const ImageViewControl* view) const
{
    const an::ui::shell::Task* task = m_impl->taskOn(view);
    if (task == nullptr)
    {
        return std::nullopt;
    }
    return an::ui::HistoryPendingStep{.processName = task->displayName, .writing = task->run.running()};
}

QString TaskPresenter::noticeTextOf(const ImageViewControl* view) const
{
    return m_impl->noticeText(view);
}

void TaskPresenter::watch(ImageViewControl& view)
{
    m_impl->watch(view);
    m_impl->refresh(&view);
}

void TaskPresenter::activeWindowChanged(QWidget* window)
{
    an::ui::shell::TaskModel& model = m_impl->model;
    const an::ui::shell::Task* active = nullptr;
    for (const an::ui::shell::Task& task : model.tasks())
    {
        // A task in a window: the process's own, or the window its page floats in. A page in the main window is not.
        if (window != nullptr && !task.process.isNull() &&
            task.presentation == an::ui::shell::TaskPresentation::Window && task.process->window() == window)
        {
            active = &task;
            break;
        }
    }
    if (active != nullptr)
    {
        (void)model.setCurrent(active->id);
        return;
    }
    // A task shown in a window is no longer the one looked at once another window is.
    if (const an::ui::shell::Task* current = model.task(model.current());
        current != nullptr && current->presentation == an::ui::shell::TaskPresentation::Window)
    {
        (void)model.setCurrent(0u);
    }
}

} // namespace an::ui::observatory
