// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "views/WorkspaceModel.h"

#include <QString>

#include <algorithm>
#include <utility>

namespace an::ui::shell
{

namespace
{

/// The default name of the workspace numbered `number`.
[[nodiscard]] QString numberedName(const int number)
{
    return QStringLiteral("Workspace %1").arg(number);
}

} // namespace

struct WorkspaceModel::Impl
{
    std::vector<Workspace> workspaces;
    QString current;
    int lastId = 0;
    std::vector<std::pair<std::size_t, Observer>> observers;
    std::size_t lastToken = 0;

    [[nodiscard]] std::vector<Workspace>::iterator find(const QString& id)
    {
        return std::find_if(workspaces.begin(), workspaces.end(),
                            [&id](const Workspace& workspace) { return workspace.id == id; });
    }

    void tell(const Change change, const QString& id) const
    {
        // An observer may leave while it is told: the list is copied first.
        const std::vector<std::pair<std::size_t, Observer>> told = observers;
        for (const auto& [token, observer] : told)
        {
            if (observer)
            {
                observer(change, id);
            }
        }
    }
};

WorkspaceModel::WorkspaceModel() : m_impl(std::make_unique<Impl>()) {}

WorkspaceModel::~WorkspaceModel() = default;

QString WorkspaceModel::create(const QString& name)
{
    Impl& impl = *m_impl;
    Workspace workspace;
    workspace.id = QString::number(++impl.lastId);
    const QString trimmed = name.trimmed();
    workspace.name = trimmed.isEmpty() ? nextName() : trimmed;
    impl.workspaces.push_back(workspace);
    impl.tell(Change::Added, workspace.id);
    if (impl.current.isEmpty())
    {
        impl.current = workspace.id;
        impl.tell(Change::Current, workspace.id);
    }
    return workspace.id;
}

QString WorkspaceModel::createWithId(const QString& id, const QString& name)
{
    Impl& impl = *m_impl;
    if (id.isEmpty() || impl.find(id) != impl.workspaces.end())
    {
        return {};
    }
    // An id of digits is one the model itself would have given: the ids it gives from here on start past it.
    bool numbered = false;
    const int number = id.toInt(&numbered);
    if (numbered && number > impl.lastId)
    {
        impl.lastId = number;
    }
    Workspace workspace;
    workspace.id = id;
    const QString trimmed = name.trimmed();
    workspace.name = trimmed.isEmpty() ? nextName() : trimmed;
    impl.workspaces.push_back(workspace);
    impl.tell(Change::Added, workspace.id);
    if (impl.current.isEmpty())
    {
        impl.current = workspace.id;
        impl.tell(Change::Current, workspace.id);
    }
    return workspace.id;
}

bool WorkspaceModel::rename(const QString& id, const QString& name)
{
    Impl& impl = *m_impl;
    const auto found = impl.find(id);
    const QString trimmed = name.trimmed();
    if (found == impl.workspaces.end() || trimmed.isEmpty())
    {
        return false;
    }
    if (found->name != trimmed)
    {
        found->name = trimmed;
        impl.tell(Change::Renamed, id);
    }
    return true;
}

bool WorkspaceModel::close(const QString& id)
{
    Impl& impl = *m_impl;
    if (!canClose(id))
    {
        return false;
    }
    const auto found = impl.find(id);
    const auto index = static_cast<std::size_t>(found - impl.workspaces.begin());
    impl.workspaces.erase(found);
    // The current one closed: the one after it, or the one before when it was the last.
    if (impl.current == id)
    {
        impl.current = impl.workspaces[(std::min)(index, impl.workspaces.size() - 1)].id;
        impl.tell(Change::Current, impl.current);
    }
    impl.tell(Change::Closed, id);
    return true;
}

bool WorkspaceModel::canClose(const QString& id) const
{
    return m_impl->workspaces.size() > 1 && find(id) != nullptr;
}

bool WorkspaceModel::move(const QString& id, const int index)
{
    Impl& impl = *m_impl;
    const auto found = impl.find(id);
    if (found == impl.workspaces.end())
    {
        return false;
    }
    Workspace moved = *found;
    impl.workspaces.erase(found);
    const auto to = static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(impl.workspaces.size())));
    impl.workspaces.insert(impl.workspaces.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    impl.tell(Change::Moved, id);
    return true;
}

bool WorkspaceModel::setCurrent(const QString& id)
{
    Impl& impl = *m_impl;
    if (impl.find(id) == impl.workspaces.end())
    {
        return false;
    }
    if (impl.current != id)
    {
        impl.current = id;
        impl.tell(Change::Current, id);
    }
    return true;
}

QString WorkspaceModel::current() const
{
    return m_impl->current;
}

bool WorkspaceModel::setLocked(const QString& id, const bool locked)
{
    Impl& impl = *m_impl;
    const auto found = impl.find(id);
    if (found == impl.workspaces.end())
    {
        return false;
    }
    if (found->locked != locked)
    {
        found->locked = locked;
        impl.tell(Change::Locked, id);
    }
    return true;
}

bool WorkspaceModel::setDetached(const QString& id, const bool detached)
{
    Impl& impl = *m_impl;
    const auto found = impl.find(id);
    if (found == impl.workspaces.end())
    {
        return false;
    }
    if (found->detached != detached)
    {
        found->detached = detached;
        impl.tell(Change::Detached, id);
    }
    return true;
}

const std::vector<Workspace>& WorkspaceModel::workspaces() const noexcept
{
    return m_impl->workspaces;
}

const Workspace* WorkspaceModel::find(const QString& id) const
{
    const auto found = std::find_if(m_impl->workspaces.begin(), m_impl->workspaces.end(),
                                    [&id](const Workspace& workspace) { return workspace.id == id; });
    return found != m_impl->workspaces.end() ? &*found : nullptr;
}

int WorkspaceModel::indexOf(const QString& id) const
{
    const Workspace* const found = find(id);
    return found != nullptr ? static_cast<int>(found - m_impl->workspaces.data()) : -1;
}

std::size_t WorkspaceModel::count() const noexcept
{
    return m_impl->workspaces.size();
}

QString WorkspaceModel::nextName() const
{
    for (int number = 1;; ++number)
    {
        const QString name = numberedName(number);
        const bool taken = std::any_of(m_impl->workspaces.begin(), m_impl->workspaces.end(),
                                       [&name](const Workspace& workspace) { return workspace.name == name; });
        if (!taken)
        {
            return name;
        }
    }
}

std::size_t WorkspaceModel::subscribe(Observer observer)
{
    const std::size_t token = ++m_impl->lastToken;
    m_impl->observers.emplace_back(token, std::move(observer));
    return token;
}

void WorkspaceModel::unsubscribe(const std::size_t token)
{
    auto& observers = m_impl->observers;
    observers.erase(
        std::remove_if(observers.begin(), observers.end(), [token](const auto& entry) { return entry.first == token; }),
        observers.end());
}

} // namespace an::ui::shell
