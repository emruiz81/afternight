// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the command palette shows: the commands a source answers a query with, in groups, and what it asks of the
/// source when a person runs a command or stars one. The palette knows no command; a source - the application's
/// command index - knows no widget.

#include <QKeySequence>
#include <QString>

#include <vector>

namespace an::ui
{

/// A command as the palette shows it in a row.
struct CommandRow
{
    QString id; ///< What the source tells the command by; unique among a query's rows.
    QString title; ///< "Histogram Transformation".
    /// What the command is beside its title - its menu, its category, a file's folder - or, for a command that cannot
    /// run now, why.
    QString subtitle;
    QString iconKey; ///< The application's icon by its name; empty for none.
    QKeySequence shortcut; ///< The keys that run it outside the palette; empty for none.
    /// Whether it can run now; a command that cannot is drawn dimmed, its subtitle says why, and it does not run.
    bool enabled = true;
    bool favoritable = false; ///< Whether the star makes it a favourite: a process.
    bool favorite = false;
    /// A quiet tag after the title - "extension" for a process that runs in a process of its own - and what its tooltip
    /// says; empty for none.
    QString tag;
    QString tagToolTip;

    [[nodiscard]] bool operator==(const CommandRow&) const = default;
};

/// A group of a query's rows under its header, best first.
struct CommandRowGroup
{
    QString id; ///< "processes"; unique among a query's groups.
    QString title; ///< "Processes".
    std::vector<CommandRow> rows;
    /// How many rows the palette shows until the group's header is clicked; 0 for every row.
    int limit = 0;

    [[nodiscard]] bool operator==(const CommandRowGroup&) const = default;
};

/// The commands a palette shows and runs. Used on the GUI thread.
class ICommandSource
{
public:
    ICommandSource() = default;
    virtual ~ICommandSource() = default;

    ICommandSource(const ICommandSource&) = delete;
    ICommandSource& operator=(const ICommandSource&) = delete;
    ICommandSource(ICommandSource&&) = delete;
    ICommandSource& operator=(ICommandSource&&) = delete;

    /// Reads what it lists now. The palette calls it as it opens, before its first query, so that a keystroke is a
    /// search over what is already held.
    virtual void refresh() {}

    /// The groups of rows a text finds, in the order they are shown, each group's rows best first; an empty text gives
    /// what a person reaches without typing.
    [[nodiscard]] virtual std::vector<CommandRowGroup> query(const QString& text) const = 0;

    /// Runs a command of the last query.
    /// @return whether it ran: false for a command it does not hold or one that cannot run now.
    virtual bool run(const QString& id) = 0;

    /// Makes a favoritable command a favourite, or a favourite no longer; nothing for any other.
    virtual void toggleFavorite(const QString& id) = 0;
};

} // namespace an::ui
