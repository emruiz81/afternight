// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>

#include <QString>

#include <AfterNightEngine/IO/SequenceDescriptor.h>

class GlobalSettingsPersistence;
class QLockFile;

namespace an::ui
{

inline QString resolveTempDirPath(const QString& localDataPath, const QString& customPath)
{
    return customPath.isEmpty() ? (localDataPath + "/temp") : customPath;
}

inline bool shouldCreateRuntimeDirectory(const bool directoryExists)
{
    return !directoryExists;
}

/// The extension a temporary sequence written in a container is given: the containers the product writes a sequence
/// in - a video read from another container written as Matroska - and none for any other, whose writer then refuses
/// the path.
inline std::string tempSequenceExtensionForFormat(const an::SequenceFormat format)
{
    switch (format)
    {
    case an::SequenceFormat::Ser:
        return ".ser";
    case an::SequenceFormat::ImageList:
        return ".anseq";
    case an::SequenceFormat::Avi:
        return ".avi";
    case an::SequenceFormat::Mp4:
        return ".mp4";
    case an::SequenceFormat::Video:
        return ".mkv";
    case an::SequenceFormat::Fits:
    case an::SequenceFormat::Dummy:
        break;
    }
    return "";
}

/// The prefix of a session's temporary directory: `AN_SESSION_` followed by the session's token.
inline constexpr std::string_view kSessionDirectoryPrefix = "AN_SESSION_";

/// The suffix of the lock file a live session holds beside its directory.
inline constexpr std::string_view kSessionLockSuffix = ".lock";

/// True when `name` is a session's temporary directory: the prefix and a token after it.
inline bool isSessionDirectoryName(const std::string_view name)
{
    return name.size() > kSessionDirectoryPrefix.size() &&
           name.compare(0, kSessionDirectoryPrefix.size(), kSessionDirectoryPrefix) == 0 &&
           name.find('.') == std::string_view::npos;
}

// How long scratch left in the shared system temp directory is kept before a startup sweep
// reclaims it. The window exists so a concurrently running second instance -- whose scratch is
// live but not owned by this process -- is never deleted out from under it.
inline constexpr std::chrono::seconds kStaleScratchRetention = std::chrono::hours(24);

// True when `name` is a scratch entry the application itself creates directly under the system
// temp directory. Deliberately strict: the sweep runs over a directory shared with every other
// program on the machine, so a false positive deletes somebody else's data.
inline bool isAfterNightScratchEntryName(const std::string_view name)
{
    if (name.empty())
    {
        return false;
    }

    const auto startsWith = [name](const std::string_view prefix) {
        return name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0;
    };

    const auto endsWith = [name](const std::string_view suffix) {
        return name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    // The drizzle scratch directory and the extension host's session root.
    if (name == "AfterNight_drizztmp" || name == "afternight-extension-host")
    {
        return true;
    }

    // Calibrated-stack spill directories.
    if (startsWith("AfterNight_calibrated_stack_spill_"))
    {
        return true;
    }

    // ImageIntegration StageUtilities raw-pixel fallback dumps.
    if (startsWith("AfterNightStackInput-") && endsWith(".andump"))
    {
        return true;
    }

    if (startsWith("afternight-extension-archive-"))
    {
        return true;
    }

    if (startsWith("afternight-retention-metadata-") && endsWith(".html"))
    {
        return true;
    }

    return false;
}

// True when an entry last touched `age` ago may be removed. A negative age means the timestamp
// lies in the future (clock skew, or a file written by a machine with a different clock) and is
// treated as fresh rather than as "very old".
inline bool isStaleScratchEntry(const std::chrono::seconds age, const std::chrono::seconds retention)
{
    return age > std::chrono::seconds::zero() && age >= retention;
}

class AFTERNIGHTCONTROLS_API RuntimeStorageCoordinator
{
public:
    struct Dependencies
    {
        QString* localDataPath = nullptr;
        std::function<GlobalSettingsPersistence*()> globalSettingsPersistence;
        std::function<std::string()> createUuidToken;
    };

    explicit RuntimeStorageCoordinator(const Dependencies& dependencies);
    ~RuntimeStorageCoordinator();

    RuntimeStorageCoordinator(const RuntimeStorageCoordinator&) = delete;
    RuntimeStorageCoordinator& operator=(const RuntimeStorageCoordinator&) = delete;

    /// Removes this session's temporary directory, everything in it, and the lock it held: what the application wrote
    /// while it ran, the histories' directories and the temporary sequences among it. Nothing else is touched.
    void cleanTemporaryFiles() const;

    /// Reclaims, on a worker, the temporary directories of earlier sessions that are no longer running: one whose lock
    /// this session can take. A session still running holds its lock, so its directory - its histories' snapshots
    /// included - is never reclaimed; nothing in the temporary root but a session's directory and its lock is touched.
    void cleanTemporaryFilesAsync() const;

    // Reclaims scratch that earlier runs left outside the managed temp directory -- the
    // directories a crash or a force-quit strands, which nothing else ever deletes.
    void cleanStaleScratchRoots() const;
    void cleanStaleScratchRootsAsync() const;

    /// Creates this session's temporary directory and takes the lock that marks it live.
    void ensureTempDirCreated() const;

    /// This session's temporary directory: `AN_SESSION_<token>` below the temporary root. Everything the application
    /// writes as temporary goes here, so only this session's files are reclaimed when it ends.
    QString getTempDirPath() const;

    /// The temporary root: the folder the settings name, or `temp` below the application's data directory.
    QString getTempRootPath() const;

    void ensureThumbnailsDirCreated() const;
    QString getThumbnailsDirPath() const;

    /// A new path in this session's temporary directory for a sequence written in a container.
    std::string createTempFilePathForNewSequence(an::SequenceFormat format) const;
    std::set<std::string> getListOfTemporaryDirs() const;

private:
    static void cleanEndedSessionsJob(std::string temporaryRoot, std::string ownSession);
    static void cleanStaleScratchRootsJob(std::string sharedBufferRoot);

    Dependencies m_dependencies;
    std::string m_sessionToken;
    mutable std::unique_ptr<QLockFile> m_sessionLock;
};

} // namespace an::ui
