// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "commands/CommandIndex.h"

#include <QChar>
#include <QHash>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <utility>

namespace an::ui::shell
{

namespace
{

/// Never run: a command no one ran comes after every one that was.
constexpr std::size_t kNeverRun = (std::numeric_limits<std::size_t>::max)();

/// The groups a typed query shows, in their order.
constexpr std::array<CommandGroup, 4u> kTypedGroups{CommandGroup::Processes, CommandGroup::Images, CommandGroup::Views,
                                                    CommandGroup::Settings};

/// A text as a search compares it: its case folded.
QString folded(const QString& text)
{
    return text.toCaseFolded();
}

/// The words of a folded text: what lies between spaces and the marks a title separates its parts with.
QStringList wordsOf(const QString& text)
{
    QStringList words;
    QString word;
    for (const QChar character : text)
    {
        if (character.isLetterOrNumber())
        {
            word.append(character);
        }
        else if (!word.isEmpty())
        {
            words.append(word);
            word.clear();
        }
    }
    if (!word.isEmpty())
    {
        words.append(word);
    }
    return words;
}

/// Whether each of `wanted` starts a word of `words`, in order, each word starting one at most.
bool startsWordsInOrder(const QStringList& words, const QStringList& wanted)
{
    if (wanted.isEmpty())
    {
        return false;
    }
    qsizetype next = 0;
    for (const QString& part : wanted)
    {
        while (next < words.size() && !words[next].startsWith(part))
        {
            ++next;
        }
        if (next == words.size())
        {
            return false;
        }
        ++next;
    }
    return true;
}

/// Whether the letters of `wanted` - its spaces left out - appear in `text` in order.
bool holdsInOrder(const QString& text, const QString& wanted)
{
    qsizetype at = 0;
    bool any = false;
    for (const QChar character : wanted)
    {
        if (character.isSpace())
        {
            continue;
        }
        any = true;
        at = text.indexOf(character, at);
        if (at < 0)
        {
            return false;
        }
        ++at;
    }
    return any;
}

/// What a command is compared by, its texts folded once when the index is refreshed.
struct Searchable
{
    QString title;
    QStringList titleWords;
    QStringList keywords;
    std::vector<QStringList> keywordWords;
};

Searchable searchableOf(const CommandEntry& entry)
{
    Searchable searchable;
    searchable.title = folded(entry.title);
    searchable.titleWords = wordsOf(searchable.title);
    for (const QString& keyword : entry.keywords)
    {
        searchable.keywords.append(folded(keyword));
        searchable.keywordWords.push_back(wordsOf(searchable.keywords.back()));
    }
    return searchable;
}

/// How a command's folded texts match a folded text.
CommandIndex::Match matchOf(const Searchable& searchable, const QString& text, const QStringList& textWords)
{
    if (text.isEmpty())
    {
        return CommandIndex::Match::None;
    }
    if (searchable.title.startsWith(text))
    {
        return CommandIndex::Match::Prefix;
    }
    if (startsWordsInOrder(searchable.titleWords, textWords))
    {
        return CommandIndex::Match::WordPrefix;
    }
    for (qsizetype keyword = 0; keyword < searchable.keywords.size(); ++keyword)
    {
        if (searchable.keywords[keyword].startsWith(text) ||
            startsWordsInOrder(searchable.keywordWords[static_cast<std::size_t>(keyword)], textWords))
        {
            return CommandIndex::Match::WordPrefix;
        }
    }
    if (holdsInOrder(searchable.title, text))
    {
        return CommandIndex::Match::Subsequence;
    }
    return CommandIndex::Match::None;
}

an::ui::CommandRow rowOf(const CommandEntry& entry)
{
    an::ui::CommandRow row;
    row.id = entry.id;
    row.title = entry.title;
    row.subtitle = entry.available ? entry.subtitle : entry.unavailableReason;
    row.iconKey = entry.iconKey;
    row.shortcut = entry.shortcut;
    row.enabled = entry.available;
    row.favoritable = static_cast<bool>(entry.setFavorite);
    row.favorite = entry.favorite;
    return row;
}

} // namespace

struct CommandIndex::Impl
{
    std::vector<std::unique_ptr<ICommandProvider>> providers;
    std::vector<CommandEntry> entries;
    std::vector<Searchable> searchables;
    QHash<QString, std::size_t> byId;

    RecentStore store;
    std::deque<QString> recent;
    QHash<QString, std::size_t> recency;

    void rememberRecency()
    {
        recency.clear();
        for (std::size_t rank = 0; rank < recent.size(); ++rank)
        {
            recency.insert(recent[rank], rank);
        }
    }

    [[nodiscard]] std::size_t recencyOf(const QString& id) const
    {
        return recency.value(id, kNeverRun);
    }

    void record(const QString& id)
    {
        recent.erase(std::remove(recent.begin(), recent.end(), id), recent.end());
        recent.push_front(id);
        while (recent.size() > kRecentLimit)
        {
            recent.pop_back();
        }
        rememberRecency();
        if (store.save)
        {
            store.save(QStringList(recent.begin(), recent.end()).join(QLatin1Char('\n')));
        }
    }

    /// Orders the entries `indices` point at by how they match, then by recency, then by title.
    void rank(std::vector<std::pair<std::size_t, Match>>& matched) const
    {
        std::stable_sort(matched.begin(), matched.end(), [this](const auto& left, const auto& right) {
            if (left.second != right.second)
            {
                return left.second < right.second;
            }
            const std::size_t leftRecency = recencyOf(entries[left.first].id);
            const std::size_t rightRecency = recencyOf(entries[right.first].id);
            if (leftRecency != rightRecency)
            {
                return leftRecency < rightRecency;
            }
            return searchables[left.first].title < searchables[right.first].title;
        });
    }

    [[nodiscard]] an::ui::CommandRowGroup
    groupOf(const CommandGroup group, const std::vector<std::pair<std::size_t, Match>>& matched, const int limit) const
    {
        an::ui::CommandRowGroup rows;
        rows.id = CommandIndex::groupId(group);
        rows.title = CommandIndex::groupTitle(group);
        rows.limit = limit;
        for (const auto& [index, match] : matched)
        {
            rows.rows.push_back(rowOf(entries[index]));
        }
        return rows;
    }
};

CommandIndex::CommandIndex() : m_impl(std::make_unique<Impl>()) {}

CommandIndex::~CommandIndex() = default;

void CommandIndex::addProvider(std::unique_ptr<ICommandProvider> provider)
{
    if (provider)
    {
        m_impl->providers.push_back(std::move(provider));
    }
}

void CommandIndex::setStore(RecentStore store)
{
    m_impl->store = std::move(store);
}

void CommandIndex::loadRecents()
{
    Impl& impl = *m_impl;
    impl.recent.clear();
    if (impl.store.load)
    {
        for (const QString& line : impl.store.load().split(QLatin1Char('\n')))
        {
            const QString id = line.trimmed();
            if (!id.isEmpty() && std::find(impl.recent.begin(), impl.recent.end(), id) == impl.recent.end() &&
                impl.recent.size() < kRecentLimit)
            {
                impl.recent.push_back(id);
            }
        }
    }
    impl.rememberRecency();
}

const std::deque<QString>& CommandIndex::recentIds() const noexcept
{
    return m_impl->recent;
}

const std::vector<CommandEntry>& CommandIndex::entries() const noexcept
{
    return m_impl->entries;
}

const CommandEntry* CommandIndex::entry(const QString& id) const
{
    const auto found = m_impl->byId.constFind(id);
    return found == m_impl->byId.cend() ? nullptr : &m_impl->entries[*found];
}

CommandIndex::ParsedQuery CommandIndex::parse(const QString& text)
{
    ParsedQuery parsed;
    QString rest = text.trimmed();
    if (!rest.isEmpty())
    {
        switch (rest.front().unicode())
        {
        case u'>':
            parsed.scope = CommandGroup::Processes;
            break;
        case u'@':
            parsed.scope = CommandGroup::Images;
            break;
        case u':':
            parsed.scope = CommandGroup::Settings;
            break;
        default:
            break;
        }
        if (parsed.scope)
        {
            rest = rest.mid(1).trimmed();
        }
    }
    parsed.text = rest;
    return parsed;
}

CommandIndex::Match CommandIndex::match(const CommandEntry& entry, const QString& text)
{
    const QString wanted = folded(text.trimmed());
    return matchOf(searchableOf(entry), wanted, wordsOf(wanted));
}

QString CommandIndex::groupId(const CommandGroup group)
{
    switch (group)
    {
    case CommandGroup::Processes:
        return QStringLiteral("processes");
    case CommandGroup::Images:
        return QStringLiteral("images");
    case CommandGroup::Views:
        return QStringLiteral("views");
    case CommandGroup::Settings:
        return QStringLiteral("settings");
    }
    return {};
}

QString CommandIndex::groupTitle(const CommandGroup group)
{
    switch (group)
    {
    case CommandGroup::Processes:
        return QStringLiteral("Processes");
    case CommandGroup::Images:
        return QStringLiteral("Images");
    case CommandGroup::Views:
        return QStringLiteral("Views");
    case CommandGroup::Settings:
        return QStringLiteral("Settings");
    }
    return {};
}

void CommandIndex::refresh()
{
    Impl& impl = *m_impl;
    std::vector<CommandEntry> entries;
    for (const std::unique_ptr<ICommandProvider>& provider : impl.providers)
    {
        provider->collect(entries);
    }
    impl.entries.clear();
    impl.searchables.clear();
    impl.byId.clear();
    for (CommandEntry& entry : entries)
    {
        // A command is listed once, as the first provider to give its id gives it.
        if (entry.id.isEmpty() || impl.byId.contains(entry.id))
        {
            continue;
        }
        impl.byId.insert(entry.id, impl.entries.size());
        impl.searchables.push_back(searchableOf(entry));
        impl.entries.push_back(std::move(entry));
    }
}

std::vector<an::ui::CommandRowGroup> CommandIndex::query(const QString& text) const
{
    const Impl& impl = *m_impl;
    const ParsedQuery parsed = parse(text);
    std::vector<an::ui::CommandRowGroup> groups;

    if (!parsed.scope && parsed.text.isEmpty())
    {
        // What a person reaches without typing: the favourites by title, then the commands run last.
        std::vector<std::pair<std::size_t, Match>> favourites;
        for (std::size_t index = 0; index < impl.entries.size(); ++index)
        {
            if (impl.entries[index].favorite && impl.entries[index].setFavorite)
            {
                favourites.emplace_back(index, Match::Prefix);
            }
        }
        std::stable_sort(favourites.begin(), favourites.end(), [&impl](const auto& left, const auto& right) {
            return impl.searchables[left.first].title < impl.searchables[right.first].title;
        });
        if (!favourites.empty())
        {
            an::ui::CommandRowGroup group = impl.groupOf(CommandGroup::Processes, favourites, 0);
            group.id = QStringLiteral("favourites");
            group.title = QStringLiteral("Favourites");
            groups.push_back(std::move(group));
        }

        an::ui::CommandRowGroup recent;
        recent.id = QStringLiteral("recent");
        recent.title = QStringLiteral("Recent");
        for (const QString& id : impl.recent)
        {
            if (recent.rows.size() == kRecentShown)
            {
                break;
            }
            if (const CommandEntry* const found = entry(id))
            {
                recent.rows.push_back(rowOf(*found));
            }
        }
        if (!recent.rows.empty())
        {
            groups.push_back(std::move(recent));
        }
        return groups;
    }

    const QString wanted = folded(parsed.text);
    const QStringList wantedWords = wordsOf(wanted);
    for (const CommandGroup group : kTypedGroups)
    {
        if (parsed.scope && *parsed.scope != group)
        {
            continue;
        }
        std::vector<std::pair<std::size_t, Match>> matched;
        for (std::size_t index = 0; index < impl.entries.size(); ++index)
        {
            if (impl.entries[index].group != group)
            {
                continue;
            }
            // A narrowed query with no text lists its whole group.
            const Match found =
                wanted.isEmpty() ? Match::Prefix : matchOf(impl.searchables[index], wanted, wantedWords);
            if (found != Match::None)
            {
                matched.emplace_back(index, found);
            }
        }
        if (matched.empty())
        {
            continue;
        }
        impl.rank(matched);
        groups.push_back(impl.groupOf(group, matched, parsed.scope ? 0 : kGroupRowLimit));
    }
    return groups;
}

bool CommandIndex::run(const QString& id)
{
    const CommandEntry* const found = entry(id);
    if (found == nullptr || !found->available || !found->run)
    {
        return false;
    }
    // The command may refresh the index as it runs: it is run from a copy.
    const std::function<bool()> runIt = found->run;
    const QString ran = found->id;
    if (!runIt())
    {
        return false;
    }
    m_impl->record(ran);
    return true;
}

void CommandIndex::toggleFavorite(const QString& id)
{
    const auto found = m_impl->byId.constFind(id);
    if (found == m_impl->byId.cend())
    {
        return;
    }
    CommandEntry& command = m_impl->entries[*found];
    if (!command.setFavorite)
    {
        return;
    }
    command.favorite = !command.favorite;
    const std::function<void(bool)> setFavorite = command.setFavorite;
    setFavorite(command.favorite);
}

} // namespace an::ui::shell
