// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Every command of the application in one index, searched without a widget: what the command palette shows.

#include "AfterNightShellCoreExport.h"

#include "components/ICommandSource.h"

#include <QKeySequence>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace an::ui::shell
{

/// The group of the palette a command is listed in when a text is typed, in the order the groups are shown.
enum class CommandGroup : std::uint8_t
{
    Processes, ///< Every process the registry holds.
    Images, ///< The open views, the recent files and opening a file.
    Views, ///< Every other action: the windows, the information windows, the console, the layout.
    Settings, ///< The settings pages and their rows.
};

/// A command of the index, as a provider gives it.
struct CommandEntry
{
    /// What the index tells the command by: a kind and a name, "action:actionOpen", "process:<descriptor id>".
    QString id;
    QString title;
    QString subtitle;
    QString iconKey;
    QKeySequence shortcut;
    CommandGroup group = CommandGroup::Views;
    /// Words it is found by besides its title - a category, a menu, a page's search words.
    QStringList keywords;
    /// Runs it; whether it ran.
    std::function<bool()> run;
    /// Whether it can run now; one that cannot is listed dimmed with `unavailableReason` for its subtitle.
    bool available = true;
    QString unavailableReason;
    /// Makes it a favourite or a favourite no longer; empty for a command that is never one.
    std::function<void(bool favorite)> setFavorite;
    bool favorite = false;
    /// The quiet tag its row shows after its title, and the tag's tooltip; empty for none.
    QString tag;
    QString tagToolTip;
};

/// What gives the index its commands. Each provider is asked again whenever the index is refreshed, so what it gives
/// is what is true then: the processes available on the image the actions act on, the views open, the files used last.
class ICommandProvider
{
public:
    ICommandProvider() = default;
    virtual ~ICommandProvider() = default;

    ICommandProvider(const ICommandProvider&) = delete;
    ICommandProvider& operator=(const ICommandProvider&) = delete;
    ICommandProvider(ICommandProvider&&) = delete;
    ICommandProvider& operator=(ICommandProvider&&) = delete;

    /// Adds the commands it holds now.
    virtual void collect(std::vector<CommandEntry>& entries) const = 0;
};

/// Every command of the application, from its providers, searched as a person types.
///
/// **A query.** A text's first character may narrow it to one group: `>` the processes, `@` the images, `:` the
/// settings; the rest is the text searched, and a narrowed query with no text lists the whole group. A command is
/// found when its title starts with the text (an exact prefix), else when each word of the text starts a word of its
/// title in order, or the text starts one of its keywords (a word prefix), else when the text's letters appear in its
/// title in order (a subsequence); case is ignored. Within each kind of match the commands run most recently come
/// first, then the rest by title. The groups follow `CommandGroup`'s order, and each shows six rows until its header is
/// clicked; a narrowed query shows its one group whole.
///
/// **An empty query** lists the favourites, when there are any, by title, then the commands run last, most recent
/// first and at most eight.
///
/// **Running.** A command that cannot run now does not run. One that ran joins the commands run last, which keep the
/// last twenty, stored as they change and read again at the next start (`setStore`).
///
/// The index is pure: no widget, no window; its providers read the application. Used on the GUI thread.
class AFTERNIGHTSHELLCORE_API CommandIndex final : public an::ui::ICommandSource
{
public:
    /// How many commands run last the index keeps, and how many of them an empty query lists.
    static constexpr std::size_t kRecentLimit = 20;
    static constexpr std::size_t kRecentShown = 8;

    /// How many rows a group of a typed query shows until its header is clicked.
    static constexpr int kGroupRowLimit = 6;

    /// Where the commands run last are kept: one id a line.
    struct RecentStore
    {
        std::function<QString()> load;
        std::function<void(const QString& ids)> save;
    };

    /// What a query asks: the group its first character narrows it to, if any, and the text searched.
    struct ParsedQuery
    {
        std::optional<CommandGroup> scope;
        QString text;

        [[nodiscard]] bool operator==(const ParsedQuery&) const = default;
    };

    /// How well a command matches a text, best first.
    enum class Match : std::uint8_t
    {
        Prefix,
        WordPrefix,
        Subsequence,
        None,
    };

    CommandIndex();
    ~CommandIndex() override;

    CommandIndex(const CommandIndex&) = delete;
    CommandIndex& operator=(const CommandIndex&) = delete;
    CommandIndex(CommandIndex&&) = delete;
    CommandIndex& operator=(CommandIndex&&) = delete;

    /// Adds a provider, asked from the next refresh on.
    void addProvider(std::unique_ptr<ICommandProvider> provider);

    /// Removes a provider the index was given, and the commands it gave: a shell's own commands go with its window.
    /// Nothing for a provider the index does not hold.
    void removeProvider(const ICommandProvider* provider);

    /// Where the commands run last are kept; an empty store keeps them in memory only.
    void setStore(RecentStore store);

    /// Reads the commands run last from the store, keeping the last twenty.
    void loadRecents();

    /// The ids of the commands run last, most recent first.
    [[nodiscard]] const std::deque<QString>& recentIds() const noexcept;

    /// The commands the last refresh collected, in the providers' order.
    [[nodiscard]] const std::vector<CommandEntry>& entries() const noexcept;

    /// The command of an id the last refresh collected; null for none.
    [[nodiscard]] const CommandEntry* entry(const QString& id) const;

    /// What a query asks.
    [[nodiscard]] static ParsedQuery parse(const QString& text);

    /// How a command's title and keywords match a text.
    [[nodiscard]] static Match match(const CommandEntry& entry, const QString& text);

    /// The group's id and the title its header shows.
    [[nodiscard]] static QString groupId(CommandGroup group);
    [[nodiscard]] static QString groupTitle(CommandGroup group);

    // ICommandSource
    void refresh() override;
    [[nodiscard]] std::vector<an::ui::CommandRowGroup> query(const QString& text) const override;
    bool run(const QString& id) override;
    void toggleFavorite(const QString& id) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::shell
