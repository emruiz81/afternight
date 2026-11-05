// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/RuntimeStorageCoordinator.h"

#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightPlatform/Logging.h>
#include <AfterNightPlatform/Text.h>

#include <QDir>
#include <QLockFile>
#include <QThreadPool>
#include <QUuid>

#include <chrono>
#include <filesystem>
#include <optional>
#include <system_error>
#include <vector>

namespace an::ui
{

namespace
{

// Windows refuses path operations past MAX_PATH unless the path carries the \\?\ prefix.
// Extension sessions can nest scratch arbitrarily deep, and remove_all fails on those paths
// without it -- reporting an error the caller then has no way to act on.
std::filesystem::path removablePath(const std::filesystem::path& path)
{
#ifdef _WIN32
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    if (ec)
    {
        absolute = path;
    }

    std::wstring native = absolute.make_preferred().wstring();
    if (native.rfind(L"\\\\?\\", 0) == 0)
    {
        return absolute;
    }
    return std::filesystem::path(L"\\\\?\\" + native);
#else
    return path;
#endif
}

// Newest write time anywhere at or below `path`. A directory whose own timestamp is old can still
// hold a file written seconds ago, so the whole subtree decides whether the entry is idle.
std::optional<std::filesystem::file_time_type> newestWriteTime(const std::filesystem::path& path)
{
    std::error_code ec;
    auto newest = std::filesystem::last_write_time(path, ec);
    if (ec)
    {
        return std::nullopt;
    }

    if (!std::filesystem::is_directory(path, ec) || ec)
    {
        return newest;
    }

    std::filesystem::recursive_directory_iterator it(path, std::filesystem::directory_options::skip_permission_denied,
                                                     ec);
    if (ec)
    {
        return newest;
    }

    for (std::filesystem::recursive_directory_iterator end; it != end && !ec; it.increment(ec))
    {
        std::error_code entryEc;
        const auto stamp = std::filesystem::last_write_time(it->path(), entryEc);
        if (!entryEc && stamp > newest)
        {
            newest = stamp;
        }
    }

    return newest;
}

// Removes `path` when nothing inside it has been touched within the retention window.
// Returns true when the entry was removed.
bool removeEntryIfStale(const std::filesystem::path& path, const std::chrono::seconds retention)
{
    const auto newest = newestWriteTime(path);
    if (!newest)
    {
        return false;
    }

    const auto age =
        std::chrono::duration_cast<std::chrono::seconds>(std::filesystem::file_time_type::clock::now() - *newest);
    if (!isStaleScratchEntry(age, retention))
    {
        return false;
    }

    std::error_code ec;
    const auto removed = std::filesystem::remove_all(removablePath(path), ec);
    if (ec)
    {
        LOG_DEBUG("Could not reclaim stale scratch entry {}: {}", path.string(), ec.message());
        return false;
    }

    LOG_INFO("Reclaimed stale scratch entry {} ({} item(s))", path.string(), removed);
    return true;
}

// Sweeps every child of `root` -- used for roots whose children are opaque per-session ids that
// no name pattern can identify.
void removeStaleChildren(const std::filesystem::path& root, const std::chrono::seconds retention)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec)
    {
        return;
    }

    std::filesystem::directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec)
    {
        return;
    }

    for (std::filesystem::directory_iterator end; it != end && !ec; it.increment(ec))
    {
        removeEntryIfStale(it->path(), retention);
    }
}

/// A token for a session's directory or a temporary file: the dependency's, or a new UUID without braces.
std::string newToken(const RuntimeStorageCoordinator::Dependencies& dependencies)
{
    if (dependencies.createUuidToken)
    {
        return dependencies.createUuidToken();
    }
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

/// The lock file a session holds beside its directory.
QString sessionLockPath(const QString& root, const std::string& directoryName)
{
    return QDir(root).filePath(QString::fromStdString(directoryName + std::string(kSessionLockSuffix)));
}

} // namespace

RuntimeStorageCoordinator::RuntimeStorageCoordinator(const Dependencies& dependencies)
    : m_dependencies(dependencies), m_sessionToken(newToken(dependencies))
{
}

RuntimeStorageCoordinator::~RuntimeStorageCoordinator() = default;

void RuntimeStorageCoordinator::cleanTemporaryFiles() const
{
    const std::filesystem::path session(getTempDirPath().toStdWString());
    std::error_code ec;
    std::filesystem::remove_all(removablePath(session), ec);
    if (ec)
    {
        LOG_DEBUG("Could not remove the session's temporary directory {}: {}", session.string(), ec.message());
    }
    // The lock goes last, so no other session takes this directory for an ended one while it is being removed.
    if (m_sessionLock)
    {
        m_sessionLock->unlock();
        m_sessionLock.reset();
    }
}

void RuntimeStorageCoordinator::cleanTemporaryFilesAsync() const
{
    const std::string root = getTempRootPath().toUtf8().toStdString();
    const std::string own = std::string(kSessionDirectoryPrefix) + m_sessionToken;
    QThreadPool::globalInstance()->start([root, own]() { cleanEndedSessionsJob(root, own); });
}

void RuntimeStorageCoordinator::cleanEndedSessionsJob(std::string temporaryRoot, std::string ownSession)
{
    const std::filesystem::path root(QString::fromUtf8(temporaryRoot).toStdWString());
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec)
    {
        return;
    }
    std::vector<std::string> sessions;
    std::filesystem::directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec);
    for (std::filesystem::directory_iterator end; !ec && it != end; it.increment(ec))
    {
        const std::string name = an::platform::wideToUtf8(it->path().filename().wstring());
        std::error_code typeEc;
        if (name != ownSession && isSessionDirectoryName(name) && it->is_directory(typeEc) && !typeEc)
        {
            sessions.push_back(name);
        }
    }
    const QString rootPath = QString::fromUtf8(temporaryRoot);
    for (const std::string& name : sessions)
    {
        // A running session holds its lock; one that ended, or crashed, holds none, and its lock can be taken.
        QLockFile lock(sessionLockPath(rootPath, name));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0))
        {
            LOG_DEBUG("Leaving the temporary directory of a running session: {}", name);
            continue;
        }
        std::error_code removeEc;
        const auto removed = std::filesystem::remove_all(
            removablePath(root / std::filesystem::path(QString::fromUtf8(name).toStdWString())), removeEc);
        if (removeEc)
        {
            LOG_DEBUG("Could not reclaim the temporary directory of an ended session {}: {}", name, removeEc.message());
        }
        else
        {
            LOG_INFO("Reclaimed the temporary directory of an ended session {} ({} item(s))", name, removed);
        }
        lock.unlock();
    }
}

void RuntimeStorageCoordinator::cleanStaleScratchRoots() const
{
    const QString localDataPath = m_dependencies.localDataPath ? *m_dependencies.localDataPath : QString();
    cleanStaleScratchRootsJob(QDir(localDataPath).filePath("SharedBuffers").toUtf8().toStdString());
}

void RuntimeStorageCoordinator::cleanStaleScratchRootsAsync() const
{
    const QString localDataPath = m_dependencies.localDataPath ? *m_dependencies.localDataPath : QString();
    const std::string sharedBufferRoot = QDir(localDataPath).filePath("SharedBuffers").toUtf8().toStdString();
    QThreadPool::globalInstance()->start([sharedBufferRoot]() { cleanStaleScratchRootsJob(sharedBufferRoot); });
}

void RuntimeStorageCoordinator::cleanStaleScratchRootsJob(std::string sharedBufferRoot)
{
    std::error_code temp_error;
    const auto systemTemp = std::filesystem::temp_directory_path(temp_error);

    // Without a resolvable system temp directory every path below would be relative to the
    // working directory, so the sweep is skipped rather than pointed somewhere unintended.
    if (!temp_error && !systemTemp.empty())
    {
        std::error_code ec;
        std::filesystem::directory_iterator it(systemTemp, std::filesystem::directory_options::skip_permission_denied,
                                               ec);
        for (std::filesystem::directory_iterator end; !ec && it != end; it.increment(ec))
        {
            if (isAfterNightScratchEntryName(it->path().filename().string()))
            {
                removeEntryIfStale(it->path(), kStaleScratchRetention);
            }
        }

        removeStaleChildren(systemTemp / "AfterNight" / "LiteSharedBuffers", kStaleScratchRetention);
    }

    // Shared image buffers are named by opaque session id, so they are swept by age alone.
    if (!sharedBufferRoot.empty())
    {
        removeStaleChildren(std::filesystem::path(sharedBufferRoot), kStaleScratchRetention);
    }
}

void RuntimeStorageCoordinator::ensureTempDirCreated() const
{
    const QString root = getTempRootPath();
    QDir().mkpath(root);
    // The lock is taken before the directory exists, so a sweep in another session never sees the directory unlocked.
    if (!m_sessionLock)
    {
        auto lock =
            std::make_unique<QLockFile>(sessionLockPath(root, std::string(kSessionDirectoryPrefix) + m_sessionToken));
        lock->setStaleLockTime(0);
        if (lock->tryLock(0))
        {
            m_sessionLock = std::move(lock);
        }
        else
        {
            LOG_WARNING("Could not lock this session's temporary directory; another session may reclaim it");
        }
    }
    QDir dir(getTempDirPath());
    if (shouldCreateRuntimeDirectory(dir.exists()))
    {
        dir.mkpath(".");
    }
}

QString RuntimeStorageCoordinator::getTempDirPath() const
{
    return QDir(getTempRootPath())
        .filePath(QString::fromStdString(std::string(kSessionDirectoryPrefix) + m_sessionToken));
}

QString RuntimeStorageCoordinator::getTempRootPath() const
{
    QString customPath;
    if (m_dependencies.globalSettingsPersistence)
    {
        if (auto* settings = m_dependencies.globalSettingsPersistence())
        {
            customPath = settings->select(SettingsKeys::GENERAL_TEMP_FILES_LOCATION, QString());
        }
    }

    const QString localDataPath = m_dependencies.localDataPath ? *m_dependencies.localDataPath : QString();
    return resolveTempDirPath(localDataPath, customPath);
}

void RuntimeStorageCoordinator::ensureThumbnailsDirCreated() const
{
    QDir dir(getThumbnailsDirPath());
    if (shouldCreateRuntimeDirectory(dir.exists()))
    {
        dir.mkpath(".");
    }
}

QString RuntimeStorageCoordinator::getThumbnailsDirPath() const
{
    const QString localDataPath = m_dependencies.localDataPath ? *m_dependencies.localDataPath : QString();
    return QDir(localDataPath).filePath("thumbnails");
}

std::string RuntimeStorageCoordinator::createTempFilePathForNewSequence(const an::SequenceFormat format) const
{
    const std::string extension = tempSequenceExtensionForFormat(format);
    return fmt::format("{}/SEQ_SWAP{}{}", getTempDirPath().toUtf8().toStdString(), newToken(m_dependencies), extension);
}

std::set<std::string> RuntimeStorageCoordinator::getListOfTemporaryDirs() const
{
    std::set<std::string> ret;

    const std::filesystem::path root(getTempRootPath().toStdWString());
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec)
    {
        return ret;
    }

    std::filesystem::directory_iterator iterate(root, ec);
    for (std::filesystem::directory_iterator end; !ec && iterate != end; iterate.increment(ec))
    {
        ret.insert(an::platform::wideToUtf8(iterate->path().wstring()));
    }

    return ret;
}

} // namespace an::ui
