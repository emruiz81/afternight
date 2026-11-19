// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "tasks/TaskModel.h"

#include "actions/ActionCatalogue.h"
#include "services/OpenProcessRegistry.h"

#include "app/ProcessRegistry.h"
#include "controls/ImageViewControl.h"
#include "controls/TopLevelProcessBaseControl.h"

#include <QCoreApplication>

#include <algorithm>
#include <functional>
#include <map>
#include <utility>

namespace an::ui::shell
{

namespace
{

/// The glyph of a descriptor: its own icon, else its action's, else the generic one.
[[nodiscard]] QString glyphOf(const process_registry::ProcessDescriptor* descriptor)
{
    if (descriptor != nullptr)
    {
        if (!descriptor->icon_key.empty())
        {
            return QString::fromStdString(descriptor->icon_key);
        }
        if (const ActionSpec* action = ActionCatalogue::specOf(descriptor->action_object_name);
            action != nullptr && action->iconKey[0] != '\0')
        {
            return QString::fromLatin1(action->iconKey);
        }
    }
    return QString::fromLatin1(TaskModel::kGenericGlyph);
}

/// The name a descriptor gives its process: its display name, once the application has named it from its action;
/// else its action's text in the catalogue; else the process name it states; empty with none.
[[nodiscard]] QString nameOf(const process_registry::ProcessDescriptor& descriptor)
{
    // A builtin descriptor is registered under its action's object name until the shell binds the actions to it.
    if (!descriptor.display_name.empty() && descriptor.display_name != descriptor.action_object_name)
    {
        return QString::fromStdString(descriptor.display_name);
    }
    if (const ActionSpec* action = ActionCatalogue::specOf(descriptor.action_object_name);
        action != nullptr && action->text[0] != '\0')
    {
        return QString::fromUtf8(action->text);
    }
    return QString::fromStdString(descriptor.process_name);
}

/// Whether a run ended with a result the person is told of: succeeded or failed. A stop says nothing.
[[nodiscard]] bool endedWithAResult(const ProcessRunState& state)
{
    return state.phase == ProcessRunState::Phase::Succeeded || state.phase == ProcessRunState::Phase::Failed;
}

/// The run of a task whose host stopped answering.
[[nodiscard]] ProcessRunState notRespondingRun()
{
    return ProcessRunState{.phase = ProcessRunState::Phase::Failed,
                           .text = QCoreApplication::translate("TaskModel", "The extension stopped responding.")};
}

} // namespace

struct TaskModel::Impl
{
    Impl(OpenProcessRegistry& registry, an::ui::extensions::IExtensionSessionFeed* feed)
        : processes(registry), sessions(feed)
    {
    }

    OpenProcessRegistry& processes;
    std::size_t registrySubscription = 0;
    an::ui::extensions::IExtensionSessionFeed* sessions = nullptr;
    std::size_t sessionsSubscription = 0;
    /// The run each session's host last told of, so a run already taken in is not taken in again once it was seen.
    std::map<std::string, ProcessRunState, std::less<>> toldRuns;
    std::vector<Task> tasks;
    TaskId nextId = 1;
    TaskId current = 0;
    std::map<std::size_t, Observer> observers;
    std::size_t nextToken = 1;

    [[nodiscard]] Task* find(const TaskId id)
    {
        const auto found = std::find_if(tasks.begin(), tasks.end(), [id](const Task& task) { return task.id == id; });
        return found != tasks.end() ? &*found : nullptr;
    }

    [[nodiscard]] Task* findForSession(const std::string_view sessionId)
    {
        if (sessionId.empty())
        {
            return nullptr;
        }
        const auto found = std::find_if(tasks.begin(), tasks.end(), [sessionId](const Task& task) {
            return task.presentation == TaskPresentation::External && task.sessionId == sessionId;
        });
        return found != tasks.end() ? &*found : nullptr;
    }

    [[nodiscard]] Task* findFor(const TopLevelProcessBaseControl* process)
    {
        if (process == nullptr)
        {
            return nullptr;
        }
        const auto found = std::find_if(tasks.begin(), tasks.end(),
                                        [process](const Task& task) { return task.process.data() == process; });
        return found != tasks.end() ? &*found : nullptr;
    }

    void tell(const Change change, const TaskId id)
    {
        // An observer may subscribe or leave while it is told: the ones told are the ones there now.
        const std::map<std::size_t, Observer> told = observers;
        for (const auto& [token, observer] : told)
        {
            if (observer && observers.contains(token))
            {
                observer(change, id);
            }
        }
    }

    void add(TopLevelProcessBaseControl* process)
    {
        if (process == nullptr || findFor(process) != nullptr)
        {
            return;
        }
        Task task;
        task.id = nextId++;
        task.process = process;
        task.descriptorId = process->descriptorId();
        task.processName = QString::fromStdString(process->processName());
        const process_registry::ProcessDescriptor* descriptor =
            task.descriptorId.empty() ? nullptr : process_registry::ProcessRegistry::instance().find(task.descriptorId);
        task.displayName = descriptor != nullptr ? nameOf(*descriptor) : QString();
        if (task.displayName.isEmpty())
        {
            task.displayName = task.processName;
        }
        task.category = descriptor != nullptr ? QString::fromStdString(descriptor->category) : QString();
        task.iconKey = glyphOf(descriptor);
        task.surface = process->processSurface();
        task.run = process->runState();
        // A process launched on no image chooses every image it reads: none is its target.
        task.singleTarget =
            descriptor == nullptr || descriptor->launch_mode == process_registry::ProcessLaunchMode::SingleImage;
        if (ImageViewControl* target = task.singleTarget ? process->processTarget() : nullptr)
        {
            task.target = target;
            task.targetName = QString::fromStdString(target->getUniqueName());
        }
        const TaskId id = task.id;
        tasks.push_back(std::move(task));
        tell(Change::Added, id);
    }

    void remove(const TopLevelProcessBaseControl* process)
    {
        const auto found = std::find_if(tasks.begin(), tasks.end(),
                                        [process](const Task& task) { return task.process.data() == process; });
        if (found == tasks.end())
        {
            return;
        }
        erase(found->id);
    }

    void erase(const TaskId id)
    {
        const auto found = std::find_if(tasks.begin(), tasks.end(), [id](const Task& task) { return task.id == id; });
        if (found == tasks.end())
        {
            return;
        }
        tasks.erase(found);
        if (current == id)
        {
            current = 0;
            tell(Change::Current, 0);
        }
        tell(Change::Removed, id);
    }

    /// A task's run moved on: a run on its way lets the last result go, and one that ended with a result while the task
    /// was not the current one leaves it unseen.
    void takeRun(Task& task, const ProcessRunState& state)
    {
        task.run = state;
        if (state.running())
        {
            task.unseenResult = false;
        }
        else if (endedWithAResult(state))
        {
            task.unseenResult = task.id != current;
        }
    }

    /// A session of the extension host started, or where it stands changed.
    void stood(const an::ui::extensions::ExtensionSessionStanding& standing)
    {
        // A session whose panel the application draws is its process's task, made when the process registered.
        if (standing.sessionId.empty() || standing.hostedPanel)
        {
            return;
        }
        Task* known = findForSession(standing.sessionId);
        if (known == nullptr)
        {
            Task task;
            task.id = nextId++;
            task.presentation = TaskPresentation::External;
            task.sessionId = standing.sessionId;
            task.descriptorId = standing.descriptorId;
            const process_registry::ProcessDescriptor* descriptor =
                task.descriptorId.empty() ? nullptr
                                          : process_registry::ProcessRegistry::instance().find(task.descriptorId);
            task.displayName = descriptor != nullptr ? nameOf(*descriptor) : QString();
            if (task.displayName.isEmpty())
            {
                task.displayName = standing.displayName;
            }
            task.processName = standing.displayName;
            task.category = descriptor != nullptr ? QString::fromStdString(descriptor->category) : QString();
            task.iconKey = glyphOf(descriptor);
            task.singleTarget = !standing.target.isNull();
            task.target = standing.target;
            if (!standing.target.isNull())
            {
                task.targetName = QString::fromStdString(standing.target->getUniqueName());
            }
            task.host = standing.host;
            task.raisesItsWindow = standing.raisesItsWindow;
            toldRuns[standing.sessionId] = standing.run;
            if (standing.host == an::ui::extensions::ExtensionHostStanding::NotResponding)
            {
                task.run = notRespondingRun();
                task.unseenResult = true;
            }
            else
            {
                takeRun(task, standing.run);
            }
            const TaskId id = task.id;
            tasks.push_back(std::move(task));
            tell(Change::Added, id);
            return;
        }

        Task& task = *known;
        bool changed = false;
        if (task.raisesItsWindow != standing.raisesItsWindow)
        {
            task.raisesItsWindow = standing.raisesItsWindow;
            changed = true;
        }
        if (task.target.data() != standing.target.data())
        {
            task.target = standing.target;
            task.singleTarget = !standing.target.isNull();
            task.targetName =
                standing.target.isNull() ? QString() : QString::fromStdString(standing.target->getUniqueName());
            task.thumbnail = QImage();
            changed = true;
        }
        if (task.host != standing.host)
        {
            task.host = standing.host;
            if (standing.host == an::ui::extensions::ExtensionHostStanding::NotResponding)
            {
                task.run = notRespondingRun();
                task.unseenResult = true;
            }
            changed = true;
        }
        // A run is taken in when the host tells of another, so one already seen is not shown again.
        ProcessRunState& told = toldRuns[standing.sessionId];
        if (told != standing.run)
        {
            told = standing.run;
            if (task.host != an::ui::extensions::ExtensionHostStanding::NotResponding)
            {
                takeRun(task, standing.run);
            }
            changed = true;
        }
        if (changed)
        {
            tell(Change::Changed, task.id);
        }
    }

    /// A session of the extension host ended: its task goes, or stays failed when its host failed.
    void ended(const an::ui::extensions::ExtensionSessionEnd& end)
    {
        Task* task = findForSession(end.sessionId);
        if (const auto told = toldRuns.find(end.sessionId); told != toldRuns.end())
        {
            toldRuns.erase(told);
        }
        if (task == nullptr)
        {
            return;
        }
        if (!end.failed)
        {
            erase(task->id);
            return;
        }
        task->hostFailed = true;
        task->sessionId.clear();
        task->raisesItsWindow = false;
        task->run = ProcessRunState{.phase = ProcessRunState::Phase::Failed, .text = end.reason};
        task->unseenResult = true;
        tell(Change::Changed, task->id);
    }
};

TaskModel::TaskModel(OpenProcessRegistry& processes) : TaskModel(processes, nullptr) {}

TaskModel::TaskModel(OpenProcessRegistry& processes, an::ui::extensions::IExtensionSessionFeed* sessions)
    : m_impl(std::make_unique<Impl>(processes, sessions))
{
    m_impl->registrySubscription = processes.subscribe([this](TopLevelProcessBaseControl* window, const bool added) {
        if (added)
        {
            m_impl->add(window);
        }
        else
        {
            m_impl->remove(window);
        }
    });
    // The windows open now are tasks too, in no order the registry keeps.
    for (TopLevelProcessBaseControl* window : processes.snapshot())
    {
        m_impl->add(window);
    }
    if (sessions != nullptr)
    {
        m_impl->sessionsSubscription = sessions->observeSessions(
            {[this](const an::ui::extensions::ExtensionSessionStanding& standing) { m_impl->stood(standing); },
             [this](const an::ui::extensions::ExtensionSessionEnd& end) { m_impl->ended(end); }});
        // And so are the sessions running now, in the order they started.
        for (const an::ui::extensions::ExtensionSessionStanding& standing : sessions->sessionStandings())
        {
            m_impl->stood(standing);
        }
    }
}

TaskModel::~TaskModel()
{
    m_impl->processes.unsubscribe(m_impl->registrySubscription);
    if (m_impl->sessions != nullptr)
    {
        m_impl->sessions->stopObservingSessions(m_impl->sessionsSubscription);
    }
}

const std::vector<Task>& TaskModel::tasks() const noexcept
{
    return m_impl->tasks;
}

const Task* TaskModel::task(const TaskId id) const
{
    return m_impl->find(id);
}

const Task* TaskModel::taskFor(const TopLevelProcessBaseControl* process) const
{
    return m_impl->findFor(process);
}

const Task* TaskModel::taskForSession(const std::string_view sessionId) const
{
    return m_impl->findForSession(sessionId);
}

std::vector<TaskId> TaskModel::tasksOn(const ImageViewControl* view) const
{
    std::vector<TaskId> on;
    if (view == nullptr)
    {
        return on;
    }
    for (const Task& task : m_impl->tasks)
    {
        if (task.target.data() == view)
        {
            on.push_back(task.id);
        }
    }
    return on;
}

const Task* TaskModel::runningTaskOn(const ImageViewControl* view) const
{
    if (view == nullptr)
    {
        return nullptr;
    }
    const auto found = std::find_if(m_impl->tasks.begin(), m_impl->tasks.end(), [view](const Task& task) {
        return task.target.data() == view && task.run.running() && task.presentation != TaskPresentation::External;
    });
    return found != m_impl->tasks.end() ? &*found : nullptr;
}

TaskId TaskModel::current() const noexcept
{
    return m_impl->current;
}

bool TaskModel::setCurrent(const TaskId id)
{
    Task* task = nullptr;
    if (id != 0)
    {
        task = m_impl->find(id);
        if (task == nullptr)
        {
            return false;
        }
    }
    const bool moved = m_impl->current != id;
    m_impl->current = id;
    // The task is shown: what its run came to is seen, and an ended run is let go - but a failed host's, which stays
    // until the task is closed.
    bool seen = false;
    if (task != nullptr && !task->hostFailed && (task->unseenResult || task->run.ended()))
    {
        task->unseenResult = false;
        task->run = ProcessRunState{};
        seen = true;
    }
    if (moved)
    {
        m_impl->tell(Change::Current, id);
    }
    if (seen)
    {
        m_impl->tell(Change::Changed, id);
    }
    return true;
}

void TaskModel::setRunState(const TopLevelProcessBaseControl& process, const ProcessRunState& state)
{
    Task* task = m_impl->findFor(&process);
    if (task == nullptr || task->run == state)
    {
        return;
    }
    m_impl->takeRun(*task, state);
    m_impl->tell(Change::Changed, task->id);
}

bool TaskModel::raiseExternal(const TaskId id)
{
    const Task* task = m_impl->find(id);
    if (task == nullptr || task->presentation != TaskPresentation::External || task->hostFailed ||
        !task->raisesItsWindow || m_impl->sessions == nullptr)
    {
        return false;
    }
    return m_impl->sessions->raiseSessionWindow(task->sessionId);
}

bool TaskModel::closeExternal(const TaskId id)
{
    const Task* task = m_impl->find(id);
    if (task == nullptr || task->presentation != TaskPresentation::External)
    {
        return false;
    }
    if (task->hostFailed)
    {
        m_impl->erase(id);
        return true;
    }
    if (m_impl->sessions == nullptr)
    {
        return false;
    }
    // The session ends as it should, and its end takes the task away.
    m_impl->sessions->closeSession(std::string(task->sessionId));
    return true;
}

void TaskModel::setTarget(const TopLevelProcessBaseControl& process, ImageViewControl* target)
{
    Task* task = m_impl->findFor(&process);
    if (task == nullptr || !task->singleTarget)
    {
        return;
    }
    const QString name = target != nullptr ? QString::fromStdString(target->getUniqueName()) : QString();
    if (task->target.data() == target && task->targetName == name)
    {
        return;
    }
    if (task->target.data() != target)
    {
        // The thumbnail was the old target's.
        task->thumbnail = QImage();
    }
    task->target = target;
    task->targetName = name;
    m_impl->tell(Change::Changed, task->id);
}

void TaskModel::setJobName(const TopLevelProcessBaseControl& process, const QString& name)
{
    Task* task = m_impl->findFor(&process);
    if (task == nullptr || !task->target.isNull() || task->targetName == name)
    {
        return;
    }
    task->targetName = name;
    m_impl->tell(Change::Changed, task->id);
}

void TaskModel::targetRenamed(const ImageViewControl* view)
{
    if (view == nullptr)
    {
        return;
    }
    const QString name = QString::fromStdString(view->getUniqueName());
    std::vector<TaskId> renamed;
    for (Task& task : m_impl->tasks)
    {
        if (task.target.data() == view && task.targetName != name)
        {
            task.targetName = name;
            renamed.push_back(task.id);
        }
    }
    for (const TaskId id : renamed)
    {
        m_impl->tell(Change::Changed, id);
    }
}

void TaskModel::setThumbnail(const TaskId id, const QImage& thumbnail)
{
    Task* task = m_impl->find(id);
    if (task == nullptr || task->thumbnail.cacheKey() == thumbnail.cacheKey())
    {
        return;
    }
    task->thumbnail = thumbnail;
    m_impl->tell(Change::Changed, id);
}

void TaskModel::setPresentation(const TaskId id, const TaskPresentation presentation)
{
    Task* task = m_impl->find(id);
    if (task == nullptr || task->presentation == presentation)
    {
        return;
    }
    task->presentation = presentation;
    m_impl->tell(Change::Changed, id);
}

std::size_t TaskModel::subscribe(Observer observer)
{
    const std::size_t token = m_impl->nextToken++;
    m_impl->observers.emplace(token, std::move(observer));
    return token;
}

void TaskModel::unsubscribe(const std::size_t token)
{
    m_impl->observers.erase(token);
}

} // namespace an::ui::shell
