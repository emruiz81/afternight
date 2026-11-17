// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The workspaces a shell shows, with no widget: their names and their order, which one is current, and whether each is
/// locked or in a window of its own.

#include "AfterNightShellCoreExport.h"

#include <QString>

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace an::ui::shell
{

/// A workspace as the model holds it.
struct Workspace
{
    QString id; ///< What the model tells it by; never given to another.
    QString name;
    bool locked = false;
    bool detached = false; ///< In a window of its own.
};

/// The list of a shell's workspaces. A workspace made is "Workspace N", N the lowest number no other is named by,
/// unless it is given a name; the first made is the current one. The only workspace is never closed; when the current
/// one closes, the one after it becomes current, or the one before when it was the last. Each change is told to the
/// observers, a change refused or one that changes nothing to none.
class AFTERNIGHTSHELLCORE_API WorkspaceModel final
{
public:
    /// What changed of a workspace.
    enum class Change
    {
        Added,
        Renamed,
        Closed,
        Moved,
        Current,
        Locked,
        Detached
    };

    using Observer = std::function<void(Change change, const QString& id)>;

    WorkspaceModel();
    ~WorkspaceModel();

    WorkspaceModel(const WorkspaceModel&) = delete;
    WorkspaceModel& operator=(const WorkspaceModel&) = delete;
    WorkspaceModel(WorkspaceModel&&) = delete;
    WorkspaceModel& operator=(WorkspaceModel&&) = delete;

    /// Adds a workspace at the end, named `name` or "Workspace N" when it is empty, and answers its id.
    QString create(const QString& name = {});
    /// Adds a workspace at the end under an id it had when it was recorded - a project's - named as `create` names it.
    /// No workspace made afterwards is given that id, nor a number an id of digits states or passes.
    ///
    /// @return the id; empty, with nothing added, for an empty id or one the model holds.
    QString createWithId(const QString& id, const QString& name = {});
    /// Renames a workspace, its name's ends trimmed; false for an empty name or an id the model does not hold.
    bool rename(const QString& id, const QString& name);
    /// Closes a workspace; false for the only one or an id the model does not hold.
    bool close(const QString& id);
    [[nodiscard]] bool canClose(const QString& id) const;
    /// Moves a workspace to `index` in the order, the last place for one past the end.
    bool move(const QString& id, int index);

    /// Makes a workspace the current one; false for an id the model does not hold.
    bool setCurrent(const QString& id);
    /// The current workspace's id; empty while the model holds none.
    [[nodiscard]] QString current() const;

    bool setLocked(const QString& id, bool locked);
    bool setDetached(const QString& id, bool detached);

    [[nodiscard]] const std::vector<Workspace>& workspaces() const noexcept;
    /// The workspace of an id; null for one the model does not hold.
    [[nodiscard]] const Workspace* find(const QString& id) const;
    /// Where a workspace is in the order; -1 for one the model does not hold.
    [[nodiscard]] int indexOf(const QString& id) const;
    [[nodiscard]] std::size_t count() const noexcept;
    /// The name the next workspace made with none would take.
    [[nodiscard]] QString nextName() const;

    /// Adds an observer, told of every change from now on; the token takes it out again.
    std::size_t subscribe(Observer observer);
    void unsubscribe(std::size_t token);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::shell
