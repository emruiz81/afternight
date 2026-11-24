// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A kept history's index and directory: the index as text, a file's identity, what an interrupted session left, a
/// step's file moved, and a history measured and compacted while no view has it open.

#include "stdafx.hpp"
#include "ImageHistoryIndex.h"

#include <AfterNightEngine/IO/PendingOutput.h>
#include <AfterNightPlatform/Sha256.h>

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <QTimeZone>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <system_error>
#include <utility>

namespace an::ui::history
{

namespace
{

constexpr char kIndexFormat[] = "afternight-history";
constexpr int kIndexVersion = 1;

/// The suffix of the file a step's record is staged in for the length of a read or a write.
constexpr char kRecordSuffix[] = ".record";

/// How often, and how far apart, an index is tried again: written, when its file cannot take its name because a reader
/// holds the file it replaces for the moment it reads it; read, when the file is taking the place of the one it read.
constexpr int kIndexWriteAttempts = 40;
constexpr unsigned long kIndexWriteRetryMs = 5u;

[[nodiscard]] QString indexPathIn(const QString& directory)
{
    return QDir(directory).filePath(QString::fromLatin1(kHistoryIndexFileName));
}

/// Whether a name is one a step's file has in its history's directory: a file's name and nothing of a path.
[[nodiscard]] bool isPlainFileName(const QString& name)
{
    return !name.isEmpty() && name != QStringLiteral(".") && name != QStringLiteral("..") &&
           !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\')) && !name.contains(QLatin1Char(':'));
}

[[nodiscard]] bool isStepFileName(const QString& name)
{
    return name.endsWith(QLatin1String(kXisfStepExtension.data(), static_cast<qsizetype>(kXisfStepExtension.size()))) ||
           name.endsWith(
               QLatin1String(kSnapshotStepExtension.data(), static_cast<qsizetype>(kSnapshotStepExtension.size())));
}

[[nodiscard]] QString timeText(const QDateTime& time)
{
    return time.isValid() ? time.toUTC().toString(Qt::ISODateWithMs) : QString();
}

[[nodiscard]] QDateTime timeOf(const QString& text)
{
    if (text.isEmpty())
    {
        return {};
    }
    QDateTime time = QDateTime::fromString(text, Qt::ISODateWithMs);
    return time.isValid() ? time.toUTC() : QDateTime();
}

/// The bytes of everything below a directory.
[[nodiscard]] std::uint64_t bytesBelow(const QString& directory)
{
    std::uint64_t bytes = 0u;
    QDirIterator entries(directory, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (entries.hasNext())
    {
        entries.next();
        bytes += static_cast<std::uint64_t>(entries.fileInfo().size());
    }
    return bytes;
}

/// What a kept history's directory is in use for.
enum class HomeState : std::uint8_t
{
    /// A view's history keeps its steps there.
    Kept,

    /// The history that kept it is finishing its writes there behind its view.
    Closing,

    /// A worker is changing the history there.
    Busy
};

/// The directories of kept histories in use, each by one user.
struct ClaimedHomes
{
    std::mutex mutex;
    std::condition_variable changed;
    std::map<QString, HomeState> directories;
};

[[nodiscard]] ClaimedHomes& closingHistories()
{
    static ClaimedHomes registry;
    return registry;
}

/// One claim of a directory. It gives the directory back when it goes, unless the history that made it has said it
/// is closing: then `noteHistoryClosed` does.
struct HomeClaim
{
    QString key;
    HomeState claimed = HomeState::Kept;

    HomeClaim(QString directoryKey, const HomeState state) : key(std::move(directoryKey)), claimed(state) {}
    HomeClaim(const HomeClaim&) = delete;
    HomeClaim& operator=(const HomeClaim&) = delete;

    ~HomeClaim()
    {
        ClaimedHomes& homes = closingHistories();
        {
            std::lock_guard lock(homes.mutex);
            const auto found = homes.directories.find(key);
            if (found != homes.directories.end() && found->second == claimed)
            {
                homes.directories.erase(found);
            }
        }
        homes.changed.notify_all();
    }
};

/// A directory as the registry names it: one name for one place.
[[nodiscard]] QString closingKey(const QString& directory)
{
    const QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(directory));
#ifdef Q_OS_WIN
    return cleaned.toLower();
#else
    return cleaned;
#endif
}

/// The steps of an index that are the image's own, by their place in it.
[[nodiscard]] std::vector<std::size_t> ownStepsOf(const HistoryIndex& index)
{
    std::vector<std::size_t> own;
    for (std::size_t step = 0u; step < index.steps.size(); ++step)
    {
        if (!index.steps[step].inherited)
        {
            own.push_back(step);
        }
    }
    return own;
}

/// What a compaction of a stored history keeps and what it removes.
struct StoredCompaction
{
    std::optional<HistoryIndex> index;
    HistoryIndex compacted;
    QStringList filesToRemove;
    HistoryCompactionResult result;
};

[[nodiscard]] StoredCompaction storedCompactionOf(const QString& directory, const HistoryCompaction& what)
{
    StoredCompaction plan;
    plan.index = readHistoryIndex(directory);
    if (!plan.index.has_value())
    {
        return plan;
    }
    const HistoryIndex& index = *plan.index;
    const std::vector<std::size_t> own = ownStepsOf(index);
    std::set<std::size_t> dropped;
    if (!own.empty())
    {
        // The state the history was left at: the step it showed, else the one the file equals, else the last.
        std::size_t current = own.size() - 1u;
        for (const QString& wanted : {index.current, index.saved})
        {
            const auto found = std::find_if(own.begin(), own.end(), [&index, &wanted](const std::size_t step) {
                return !wanted.isEmpty() && index.steps[step].id == wanted;
            });
            if (found != own.end())
            {
                current = static_cast<std::size_t>(found - own.begin());
                break;
            }
        }
        std::size_t first = current;
        std::size_t last = current;
        if (what.keep == HistoryCompaction::Keep::LastSteps)
        {
            const std::size_t kept = (std::max)(what.steps, std::size_t{1});
            first = (std::min)(current, own.size() > kept ? own.size() - kept : std::size_t{0});
            last = own.size() - 1u;
        }
        for (std::size_t position = 0u; position < own.size(); ++position)
        {
            if (position < first || position > last)
            {
                dropped.insert(own[position]);
            }
        }
    }

    QSet<QString> keptFiles;
    for (std::size_t step = 0u; step < index.steps.size(); ++step)
    {
        if (dropped.count(step) == 0u)
        {
            plan.compacted.steps.push_back(index.steps[step]);
            keptFiles.insert(index.steps[step].file);
        }
    }
    plan.compacted.current = index.current;
    plan.compacted.saved = index.saved;
    plan.compacted.file = index.file;
    plan.compacted.identity = index.identity;

    QSet<QString> removed;
    for (const std::size_t step : dropped)
    {
        const QString& file = index.steps[step].file;
        // A rename shares the file of the state it renames: the file goes only when no step that stays names it.
        if (file.isEmpty() || keptFiles.contains(file) || removed.contains(file))
        {
            continue;
        }
        removed.insert(file);
        plan.filesToRemove.push_back(file);
        plan.result.bytesFreed += static_cast<std::uint64_t>(QFileInfo(QDir(directory).filePath(file)).size());
    }
    plan.result.stepsRemoved = dropped.size();
    if (what.removeDetached)
    {
        plan.result.bytesFreed += bytesBelow(QDir(directory).filePath(QString::fromLatin1(kDetachedHistoryFolder)));
    }
    plan.result.ok = true;
    return plan;
}

} // namespace

std::optional<FileIdentity> readFileIdentity(const QString& path)
{
    if (path.isEmpty())
    {
        return std::nullopt;
    }
    const QFileInfo info(path);
    if (!info.isFile())
    {
        return std::nullopt;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }
    FileIdentity identity;
    identity.size = static_cast<std::uint64_t>(info.size());
    identity.modifiedMs = info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();

    an::platform::Sha256 digest;
    std::uint64_t remaining = kFileIdentityHeadBytes;
    QByteArray block;
    while (remaining > 0u)
    {
        block = file.read(static_cast<qint64>((std::min)(remaining, std::uint64_t{64u} * 1024u)));
        if (block.isEmpty())
        {
            break;
        }
        digest.update(std::as_bytes(std::span<const char>(block.constData(), static_cast<std::size_t>(block.size()))));
        remaining -= static_cast<std::uint64_t>(block.size());
    }
    if (file.error() != QFileDevice::NoError)
    {
        return std::nullopt;
    }
    identity.headSha256 = QString::fromStdString(an::platform::sha256Hex(digest.finish()));
    return identity;
}

QByteArray formatHistoryIndex(const HistoryIndex& index)
{
    QJsonArray steps;
    for (const HistoryIndexStep& step : index.steps)
    {
        QJsonObject listed;
        listed.insert(QStringLiteral("id"), step.id);
        listed.insert(QStringLiteral("name"), step.name);
        listed.insert(QStringLiteral("description"), step.description);
        listed.insert(QStringLiteral("file"), step.file);
        listed.insert(QStringLiteral("run"), step.run);
        listed.insert(QStringLiteral("image"), step.image);
        listed.insert(QStringLiteral("time"), timeText(step.time));
        listed.insert(QStringLiteral("inherited"), step.inherited);
        listed.insert(QStringLiteral("bytes"), static_cast<qint64>(step.bytes));
        steps.append(listed);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QString::fromLatin1(kIndexFormat));
    root.insert(QStringLiteral("version"), kIndexVersion);
    root.insert(QStringLiteral("file"), index.file);
    if (index.identity.has_value())
    {
        QJsonObject identity;
        identity.insert(QStringLiteral("size"), static_cast<qint64>(index.identity->size));
        identity.insert(QStringLiteral("modified"), index.identity->modifiedMs);
        identity.insert(QStringLiteral("headSha256"), index.identity->headSha256);
        root.insert(QStringLiteral("identity"), identity);
    }
    else
    {
        root.insert(QStringLiteral("identity"), QJsonValue());
    }
    root.insert(QStringLiteral("current"), index.current);
    root.insert(QStringLiteral("saved"), index.saved);
    root.insert(QStringLiteral("steps"), steps);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<HistoryIndex> parseHistoryIndex(const QByteArray& text)
{
    const QJsonDocument document = QJsonDocument::fromJson(text);
    if (!document.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    if (root.value(QLatin1String("format")).toString() != QLatin1String(kIndexFormat) ||
        root.value(QLatin1String("version")).toInt(-1) != kIndexVersion ||
        !root.value(QLatin1String("steps")).isArray())
    {
        return std::nullopt;
    }
    HistoryIndex index;
    index.file = root.value(QLatin1String("file")).toString();
    index.current = root.value(QLatin1String("current")).toString();
    index.saved = root.value(QLatin1String("saved")).toString();
    if (const QJsonValue stated = root.value(QLatin1String("identity")); stated.isObject())
    {
        const QJsonObject identity = stated.toObject();
        const qint64 size = identity.value(QLatin1String("size")).toInteger(-1);
        const QString hash = identity.value(QLatin1String("headSha256")).toString();
        if (size < 0 || hash.isEmpty() || !identity.value(QLatin1String("modified")).isDouble())
        {
            return std::nullopt;
        }
        index.identity = FileIdentity{.size = static_cast<std::uint64_t>(size),
                                      .modifiedMs = identity.value(QLatin1String("modified")).toInteger(),
                                      .headSha256 = hash};
    }
    for (const QJsonValue& entry : root.value(QLatin1String("steps")).toArray())
    {
        if (!entry.isObject())
        {
            return std::nullopt;
        }
        const QJsonObject listed = entry.toObject();
        HistoryIndexStep step;
        step.id = listed.value(QLatin1String("id")).toString();
        step.name = listed.value(QLatin1String("name")).toString();
        step.description = listed.value(QLatin1String("description")).toString();
        step.file = listed.value(QLatin1String("file")).toString();
        step.run = listed.value(QLatin1String("run")).toString();
        step.image = listed.value(QLatin1String("image")).toString();
        step.time = timeOf(listed.value(QLatin1String("time")).toString());
        step.inherited = listed.value(QLatin1String("inherited")).toBool(false);
        const qint64 bytes = listed.value(QLatin1String("bytes")).toInteger(0);
        step.bytes = bytes > 0 ? static_cast<std::uint64_t>(bytes) : 0u;
        // A step is named, and its file is a file of the history's directory: nothing an index states reaches
        // outside it.
        const bool filed =
            step.inherited ? step.file.isEmpty() : (isPlainFileName(step.file) && isStepFileName(step.file));
        if (step.id.isEmpty() || !filed)
        {
            return std::nullopt;
        }
        index.steps.push_back(std::move(step));
    }
    return index;
}

bool writeHistoryIndex(const QString& directory, const HistoryIndex& index)
{
    const QByteArray text = formatHistoryIndex(index);
    for (int attempt = 0; attempt < kIndexWriteAttempts; ++attempt)
    {
        QSaveFile file(indexPathIn(directory));
        if (file.open(QIODevice::WriteOnly) && file.write(text) == text.size() && file.commit())
        {
            return true;
        }
        file.cancelWriting();
        QThread::msleep(kIndexWriteRetryMs);
    }
    return false;
}

std::optional<HistoryIndex> readHistoryIndex(const QString& directory)
{
    const QString path = indexPathIn(directory);
    QFile file(path);
    // An index written again takes the old one's name, and for that moment the file is there but does not open: it is
    // tried again, as a writer tries again when a reader holds the file. One that is not there is none.
    for (int attempt = 1; !file.open(QIODevice::ReadOnly); ++attempt)
    {
        if (attempt >= kIndexWriteAttempts || !QFileInfo::exists(path))
        {
            return std::nullopt;
        }
        QThread::msleep(kIndexWriteRetryMs);
    }
    // An index is a few kilobytes a step; anything far beyond that is not one.
    if (file.size() > qint64{64} * 1024 * 1024)
    {
        return std::nullopt;
    }
    return parseHistoryIndex(file.readAll());
}

HistorySweep sweepHistoryDirectory(const QString& directory, const HistoryIndex* index)
{
    HistorySweep swept;
    QSet<QString> listed;
    if (index != nullptr)
    {
        for (const HistoryIndexStep& step : index->steps)
        {
            listed.insert(step.file);
        }
    }
    const QDir home(directory);
    const QString indexName = QString::fromLatin1(kHistoryIndexFileName);
    const QStringList files = home.entryList(QDir::Files | QDir::Hidden);
    for (const QString& name : files)
    {
        if (name == indexName)
        {
            continue;
        }
        const std::string utf8 = name.toStdString();
        if (name.endsWith(QLatin1String(kRecordSuffix)))
        {
            swept.records += QFile::remove(home.filePath(name)) ? 1u : 0u;
        }
        else if (an::isPendingOutputPath(utf8) || name.startsWith(indexName + QLatin1Char('.')))
        {
            // A step's payload that never took its name, or an index that never did.
            swept.pending += QFile::remove(home.filePath(name)) ? 1u : 0u;
        }
        else if (isStepFileName(name) && !listed.contains(name))
        {
            swept.unlisted += QFile::remove(home.filePath(name)) ? 1u : 0u;
        }
    }
    return swept;
}

std::optional<StepFileMove> moveStepFile(const std::filesystem::path& from, const std::filesystem::path& to,
                                         const bool renameAllowed)
{
    std::error_code error;
    if (renameAllowed)
    {
        std::filesystem::rename(from, to, error);
        if (!error)
        {
            return StepFileMove::Renamed;
        }
        error.clear();
    }
    // Copied beside its place under the name of a payload not yet published, so the file is whole when it takes its
    // name; the original goes only after that.
    std::filesystem::path pending = to;
    pending += std::string(an::kPendingOutputSuffix);
    std::filesystem::copy_file(from, pending, std::filesystem::copy_options::overwrite_existing, error);
    if (!error)
    {
        std::filesystem::rename(pending, to, error);
    }
    if (error)
    {
        std::error_code ignored;
        std::filesystem::remove(pending, ignored);
        return std::nullopt;
    }
    std::filesystem::remove(from, error);
    return StepFileMove::Copied;
}

StoredHistorySize measureStoredHistory(const QString& directory)
{
    StoredHistorySize size;
    const QDir home(directory);
    const QFileInfoList files = home.entryInfoList(QDir::Files | QDir::Hidden);
    for (const QFileInfo& file : files)
    {
        size.bytes += static_cast<std::uint64_t>(file.size());
    }
    size.detachedBytes = bytesBelow(home.filePath(QString::fromLatin1(kDetachedHistoryFolder)));
    if (const std::optional<HistoryIndex> index = readHistoryIndex(directory); index.has_value())
    {
        size.steps = ownStepsOf(*index).size();
    }
    return size;
}

HistoryCompactionResult planStoredHistoryCompaction(const QString& directory, const HistoryCompaction& what)
{
    return storedCompactionOf(directory, what).result;
}

HistoryCompactionResult compactStoredHistory(const QString& directory, const HistoryCompaction& what)
{
    // Held for as long as it takes: a view that opens the image meanwhile waits, and a view that has it open keeps
    // it.
    bool kept = false;
    const std::shared_ptr<void> held = detail::claimHistoryHome(directory, detail::HomeUse::Worker, {}, kept);
    if (!held)
    {
        return {};
    }
    StoredCompaction plan = storedCompactionOf(directory, what);
    if (!plan.result.ok)
    {
        return plan.result;
    }
    // The index first: a process that dies before the files go leaves files no index lists, which the next open of
    // the directory removes.
    if (!writeHistoryIndex(directory, plan.compacted))
    {
        HistoryCompactionResult refused;
        return refused;
    }
    const QDir home(directory);
    for (const QString& file : plan.filesToRemove)
    {
        (void)QFile::remove(home.filePath(file));
    }
    if (what.removeDetached)
    {
        (void)QDir(home.filePath(QString::fromLatin1(kDetachedHistoryFolder))).removeRecursively();
    }
    return plan.result;
}

bool waitForClosingHistories(const QString& directory, const int timeoutMs)
{
    ClaimedHomes& registry = closingHistories();
    const QString key = directory.isEmpty() ? QString() : closingKey(directory);
    std::unique_lock lock(registry.mutex);
    // A directory a view's history keeps is not one that is finishing.
    const auto none = [&registry, &key]() {
        if (!key.isEmpty())
        {
            const auto found = registry.directories.find(key);
            return found == registry.directories.end() || found->second == HomeState::Kept;
        }
        return std::all_of(registry.directories.begin(), registry.directories.end(),
                           [](const auto& claimed) { return claimed.second == HomeState::Kept; });
    };
    if (timeoutMs < 0)
    {
        registry.changed.wait(lock, none);
        return true;
    }
    return registry.changed.wait_for(lock, std::chrono::milliseconds(timeoutMs), none);
}

namespace detail
{

std::shared_ptr<void> claimHistoryHome(const QString& directory, const HomeUse use, const std::stop_token& stop,
                                       bool& kept)
{
    kept = false;
    ClaimedHomes& registry = closingHistories();
    const QString key = closingKey(directory);
    const HomeState claimed = use == HomeUse::History ? HomeState::Kept : HomeState::Busy;
    {
        std::unique_lock lock(registry.mutex);
        while (true)
        {
            const auto found = registry.directories.find(key);
            if (found == registry.directories.end())
            {
                break;
            }
            if (found->second == HomeState::Kept)
            {
                kept = true;
                return nullptr;
            }
            // Finishing, or a worker's: it is free soon. The stop is looked at between short waits.
            (void)registry.changed.wait_for(lock, std::chrono::milliseconds(50));
            if (stop.stop_requested())
            {
                return nullptr;
            }
        }
        registry.directories.emplace(key, claimed);
    }
    return std::make_shared<HomeClaim>(key, claimed);
}

void noteHistoryClosing(const QString& directory)
{
    ClaimedHomes& registry = closingHistories();
    std::lock_guard lock(registry.mutex);
    registry.directories[closingKey(directory)] = HomeState::Closing;
}

void noteHistoryClosed(const QString& directory)
{
    ClaimedHomes& registry = closingHistories();
    {
        std::lock_guard lock(registry.mutex);
        const auto found = registry.directories.find(closingKey(directory));
        if (found != registry.directories.end() && found->second == HomeState::Closing)
        {
            registry.directories.erase(found);
        }
    }
    registry.changed.notify_all();
}

} // namespace detail

} // namespace an::ui::history
