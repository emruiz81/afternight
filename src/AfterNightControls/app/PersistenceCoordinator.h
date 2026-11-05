// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "app/FilePolicies.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <QString>

class FolderTrackerPersistence;
class GlobalSettingsPersistence;
class PersistenceDbExecutor;
class ProcessSettingsPersistence;
class ThumbnailPersistence;

namespace an::ui
{

/// Whether the folder last opened is left out of the settings: a folder on another machine is never remembered, in any
/// spelling a network path reaches the store in - the backslashes Windows writes, the slashes Qt turns them into, or a
/// file URL naming a host.
///
/// @param path the folder, in UTF-8.
/// @return true when it names a folder on another machine.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool shouldSkipPersistLastOpenDirectory(const std::string& path);

/// Opens the application's settings database and keeps what the file workflows read of it: the last folder opened, the
/// RAW policy every open reads under and the export settings every save is written with.
///
/// The database is read only at the format this version writes. One of another format is set aside beside itself and a
/// new one made in its place (`settingsStoreNotice` says so), and a new database is made directly at the format.
///
/// The RAW policy is published, not written into any engine: `rawOpenPolicy` hands out the policy built last, an open
/// keeps the one it was submitted with, and `setRawOpenSettings` publishes a new one for the opens submitted after it.
class AFTERNIGHTCONTROLS_API PersistenceCoordinator
{
public:
    struct Dependencies
    {
        QString* localDataPath = nullptr;
        std::string* lastOpenDirectory = nullptr;

        std::unique_ptr<PersistenceDbExecutor>* dbExecutor = nullptr;
        std::unique_ptr<ThumbnailPersistence>* thumbnailPersistence = nullptr;
        std::unique_ptr<FolderTrackerPersistence>* folderTrackerPersistence = nullptr;
        std::unique_ptr<ProcessSettingsPersistence>* processSettingsPersistence = nullptr;
        std::unique_ptr<GlobalSettingsPersistence>* globalSettingsPersistence = nullptr;
        std::function<void(ThumbnailPersistence*, FolderTrackerPersistence*, GlobalSettingsPersistence*)>
            setFileManagerPersistenceDependencies;
        std::function<void()> loadRecentFilesFromDisk;
        std::function<void()> ensureTempDirCreated;
        std::function<void()> ensureThumbnailsDirCreated;
    };

    explicit PersistenceCoordinator(const Dependencies& dependencies);

    void initializePersistence() const;
    void persistLastOpenDir() const;

    /// What a person should be told about the settings database the last `initializePersistence` opened: that one of
    /// another format was set aside, and where; empty when there is nothing to tell.
    [[nodiscard]] const QString& settingsStoreNotice() const noexcept;

    /// Reads the RAW settings and publishes the policy every open submitted from now on reads under.
    void loadRawOpenPolicy() const;

    /// The RAW policy published last; before the settings are read, one built from their defaults. Null only when no
    /// registry could be built, in which case an open reads through the session's own.
    [[nodiscard]] std::shared_ptr<const RawOpenPolicy> rawOpenPolicy() const;

    /// The RAW settings the published policy was built from.
    [[nodiscard]] RawOpenSettings rawOpenSettings() const;

    /// Stores the RAW settings and publishes their policy for the opens submitted after it; an open on its way keeps
    /// the policy it started with.
    ///
    /// @param settings the settings.
    /// @return success, or why the policy could not be built - the settings are stored either way.
    an::Status setRawOpenSettings(const RawOpenSettings& settings) const;

    /// The export settings as they are stored now.
    [[nodiscard]] ExportSettings exportSettings() const;

    /// The output intent the export settings give for writing an image described by `source` into `container`, which
    /// is what the application's `IOutputIntentProvider` answers.
    ///
    /// @return the intent, or why none can be built: a container the product does not write, a mosaic one cannot hold,
    ///         or an intent the engine refuses.
    [[nodiscard]] an::Result<an::OutputIntent> buildOutputIntent(const an::ImageDescriptor& source,
                                                                 an::CodecFormat container) const;

private:
    [[nodiscard]] SettingReader settingReader() const;
    void publish(std::shared_ptr<const RawOpenPolicy> policy) const;

    Dependencies m_dependencies;
    mutable QString m_settingsStoreNotice;
    mutable std::mutex m_policyMutex;
    mutable std::shared_ptr<const RawOpenPolicy> m_rawOpenPolicy;
};

} // namespace an::ui
