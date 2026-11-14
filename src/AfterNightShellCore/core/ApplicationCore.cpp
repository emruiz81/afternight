// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "core/ApplicationCore.h"

#include "services/AppEventBus.h"
#include "services/FavoriteProcessesModel.h"
#include "services/OpenProcessRegistry.h"
#include "services/RecentFilesModel.h"
#include "services/RecentProcessesModel.h"

#include "app/PersistenceCoordinator.h"
#include "app/RuntimeStorageCoordinator.h"
#include "controls/FolderTrackerPersistence.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/IAppContext.h"
#include "controls/PersistenceDbExecutor.h"
#include "controls/ProcessSettingsPersistence.h"
#include "controls/ThumbnailPersistence.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <AfterNightPlatform/Text.h>

#include <QUuid>

#include <optional>

namespace an::ui::shell
{

namespace
{

/// The global settings store as the settings interface answers it, over whichever store is open: a read of a closed
/// store answers the default, and a write to one is refused.
class StoreSettings final : public an::ui::IGlobalSettingsProvider
{
public:
    void setStore(GlobalSettingsPersistence* store) noexcept
    {
        m_store = store;
    }

    bool upsert(const QString& key, const QString& value) override
    {
        return m_store != nullptr && m_store->upsert(key, value);
    }

    [[nodiscard]] QString select(const QString& key, const QString& defaultValue = {}) override
    {
        return m_store != nullptr ? m_store->select(key, defaultValue) : defaultValue;
    }

    [[nodiscard]] std::optional<QString> selectOptional(const QString& key) override
    {
        return m_store != nullptr ? m_store->selectOptional(key) : std::nullopt;
    }

private:
    GlobalSettingsPersistence* m_store = nullptr;
};

} // namespace

struct ApplicationCore::Impl
{
    std::thread::id guiThread = std::this_thread::get_id();

    QString localDataPath;
    std::string lastOpenDirectory;

    std::unique_ptr<PersistenceDbExecutor> dbExecutor;
    std::unique_ptr<ThumbnailPersistence> thumbnailPersistence;
    std::unique_ptr<FolderTrackerPersistence> folderTrackerPersistence;
    std::unique_ptr<ProcessSettingsPersistence> processSettingsPersistence;
    std::unique_ptr<GlobalSettingsPersistence> globalSettingsPersistence;
    StoreSettings settings;

    std::unique_ptr<an::ui::PersistenceCoordinator> persistence;
    std::unique_ptr<an::ui::RuntimeStorageCoordinator> runtimeStorage;

    AppEventBus eventBus;
    OpenProcessRegistry openProcesses;
    RecentFilesModel recentFiles;
    RecentProcessesModel recentProcesses;
    FavoriteProcessesModel favoriteProcesses;

    IApplicationEventTarget* eventTarget = nullptr;
};

ApplicationCore::ApplicationCore() : QObject(nullptr), m_impl(std::make_unique<Impl>())
{
    Impl& impl = *m_impl;
    impl.recentProcesses.setSettings(&impl.settings);
    impl.favoriteProcesses.setSettings(&impl.settings);

    an::ui::RuntimeStorageCoordinator::Dependencies runtimeStorageDeps;
    runtimeStorageDeps.localDataPath = &impl.localDataPath;
    runtimeStorageDeps.globalSettingsPersistence = [&impl]() { return impl.globalSettingsPersistence.get(); };
    runtimeStorageDeps.createUuidToken = []() { return QUuid::createUuid().toString().toUtf8().toStdString(); };
    impl.runtimeStorage = std::make_unique<an::ui::RuntimeStorageCoordinator>(runtimeStorageDeps);

    an::ui::PersistenceCoordinator::Dependencies persistenceDeps;
    persistenceDeps.localDataPath = &impl.localDataPath;
    persistenceDeps.lastOpenDirectory = &impl.lastOpenDirectory;
    persistenceDeps.dbExecutor = &impl.dbExecutor;
    persistenceDeps.thumbnailPersistence = &impl.thumbnailPersistence;
    persistenceDeps.folderTrackerPersistence = &impl.folderTrackerPersistence;
    persistenceDeps.processSettingsPersistence = &impl.processSettingsPersistence;
    persistenceDeps.globalSettingsPersistence = &impl.globalSettingsPersistence;
    // The stores are made: the settings read through the store from here, and the recent files are kept in the data
    // folder, before the list is read.
    persistenceDeps.setFileManagerPersistenceDependencies = [&impl](ThumbnailPersistence*, FolderTrackerPersistence*,
                                                                    GlobalSettingsPersistence* globalSettings) {
        impl.settings.setStore(globalSettings);
        impl.recentFiles.setStorage(impl.localDataPath, &impl.settings);
    };
    persistenceDeps.loadRecentFilesFromDisk = [&impl]() { impl.recentFiles.load(); };
    persistenceDeps.ensureTempDirCreated = [&impl]() { impl.runtimeStorage->ensureTempDirCreated(); };
    persistenceDeps.ensureThumbnailsDirCreated = [&impl]() { impl.runtimeStorage->ensureThumbnailsDirCreated(); };
    impl.persistence = std::make_unique<an::ui::PersistenceCoordinator>(persistenceDeps);
}

ApplicationCore::~ApplicationCore()
{
    m_impl->eventTarget = nullptr;
    closeStores();
}

void ApplicationCore::openStores()
{
    if (storesOpen())
    {
        return;
    }
    m_impl->persistence->initializePersistence();
}

void ApplicationCore::closeStores() noexcept
{
    Impl& impl = *m_impl;
    impl.settings.setStore(nullptr);
    // Ensure right order
    impl.folderTrackerPersistence.reset();
    impl.thumbnailPersistence.reset();
    impl.processSettingsPersistence.reset();
    impl.globalSettingsPersistence.reset();
    // Shut down the DB executors after all persistence objects that depend on them.
    if (impl.dbExecutor)
    {
        impl.dbExecutor->shutdown();
    }
    impl.dbExecutor.reset();
}

bool ApplicationCore::storesOpen() const noexcept
{
    return m_impl->globalSettingsPersistence != nullptr;
}

const QString& ApplicationCore::settingsStoreNotice() const noexcept
{
    return m_impl->persistence->settingsStoreNotice();
}

const QString& ApplicationCore::localDataPath() const noexcept
{
    return m_impl->localDataPath;
}

ThumbnailPersistence* ApplicationCore::thumbnailStore() const noexcept
{
    return m_impl->thumbnailPersistence.get();
}

FolderTrackerPersistence* ApplicationCore::folderStore() const noexcept
{
    return m_impl->folderTrackerPersistence.get();
}

ProcessSettingsPersistence* ApplicationCore::processSettingsStore() const noexcept
{
    return m_impl->processSettingsPersistence.get();
}

GlobalSettingsPersistence* ApplicationCore::globalSettingsStore() const noexcept
{
    return m_impl->globalSettingsPersistence.get();
}

an::ui::IGlobalSettingsProvider* ApplicationCore::globalSettings() noexcept
{
    return &m_impl->settings;
}

std::shared_ptr<const an::ui::RawOpenPolicy> ApplicationCore::rawOpenPolicy() const
{
    return storesOpen() ? m_impl->persistence->rawOpenPolicy() : nullptr;
}

an::Status ApplicationCore::setRawOpenSettings(const an::ui::RawOpenSettings& settings)
{
    if (!storesOpen())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized,
                                   "the RAW settings cannot be stored before the settings store is open");
    }
    return m_impl->persistence->setRawOpenSettings(settings);
}

an::Result<an::OutputIntent> ApplicationCore::buildOutputIntent(const an::ImageDescriptor& source,
                                                                const an::CodecFormat container) const
{
    if (!storesOpen())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized,
                                   "the export settings cannot be read before the settings store is open");
    }
    return m_impl->persistence->buildOutputIntent(source, container);
}

const std::string& ApplicationCore::lastOpenDirectory() const noexcept
{
    return m_impl->lastOpenDirectory;
}

std::string& ApplicationCore::lastOpenDirectoryForEdit() noexcept
{
    return m_impl->lastOpenDirectory;
}

void ApplicationCore::setLastOpenDirectory(const QString& path)
{
    m_impl->lastOpenDirectory = path.toUtf8().toStdString();
    persistLastOpenDirectory();
}

void ApplicationCore::persistLastOpenDirectory()
{
    m_impl->persistence->persistLastOpenDir();
}

an::ui::RuntimeStorageCoordinator& ApplicationCore::runtimeStorage() noexcept
{
    return *m_impl->runtimeStorage;
}

bool ApplicationCore::isTempFile(const std::string& fileName) const
{
    return an::platform::startsWith(fileName, m_impl->runtimeStorage->getTempDirPath().toUtf8().toStdString(),
                                    an::platform::CaseSensitivity::Insensitive);
}

AppEventBus& ApplicationCore::eventBus() noexcept
{
    return m_impl->eventBus;
}

OpenProcessRegistry& ApplicationCore::openProcesses() noexcept
{
    return m_impl->openProcesses;
}

RecentFilesModel& ApplicationCore::recentFiles() noexcept
{
    return m_impl->recentFiles;
}

RecentProcessesModel& ApplicationCore::recentProcesses() noexcept
{
    return m_impl->recentProcesses;
}

FavoriteProcessesModel& ApplicationCore::favoriteProcesses() noexcept
{
    return m_impl->favoriteProcesses;
}

std::thread::id ApplicationCore::guiThreadId() const noexcept
{
    return m_impl->guiThread;
}

bool ApplicationCore::isGuiThread() const noexcept
{
    return std::this_thread::get_id() == m_impl->guiThread;
}

void ApplicationCore::setEventTarget(IApplicationEventTarget* target) noexcept
{
    m_impl->eventTarget = target;
}

IApplicationEventTarget* ApplicationCore::eventTarget() const noexcept
{
    return m_impl->eventTarget;
}

} // namespace an::ui::shell
