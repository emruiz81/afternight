// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "core/ApplicationCore.h"

#include "actions/ActionBindings.h"
#include "actions/ActionCatalogue.h"
#include "actions/ActionStateCoordinator.h"
#include "actions/ProcessMenuModel.h"
#include "core/IShell.h"
#include "services/AppEventBus.h"
#include "services/DialogService.h"
#include "services/ExtensionService.h"
#include "services/FavoriteProcessesModel.h"
#include "services/FileDialogService.h"
#include "services/FileDropHandler.h"
#include "services/ProcessLaunchService.h"
#include "services/OpenProcessRegistry.h"
#include "services/RecentFilesModel.h"
#include "services/RecentProcessesModel.h"
#include "views/DockHooks.h"
#include "views/DockLayoutOperations.h"
#include "views/ViewService.h"

#include "app/IconManager.h"

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
    IShell* shell = nullptr;
    an::ui::IEngineSessionProvider* session = nullptr;

    std::shared_ptr<IconManager> iconManager = std::make_shared<IconManager>();
    // The actions and their state are made after the services and go after them: a service that goes may still ask
    // for the actions' state.
    std::unique_ptr<ActionCatalogue> catalogue;
    std::unique_ptr<ActionBindings> bindings;
    std::unique_ptr<ActionStateCoordinator> actionState;
    std::unique_ptr<ProcessMenuModel> processMenus;
    std::unique_ptr<DockLayoutOperations> dockLayout;
    FileDropHandler fileDrops;
    std::unique_ptr<DialogService> dialogs;
    std::unique_ptr<FileDialogService> fileDialogs;
    std::unique_ptr<ViewService> views;
    std::unique_ptr<ProcessLaunchService> launches;
    std::unique_ptr<ExtensionService> extensions;
};

ApplicationCore::ApplicationCore() : QObject(nullptr), m_impl(std::make_unique<Impl>())
{
    // The dock library takes its configuration only before the first dock widget or main window exists: it is given
    // here, before any shell makes one.
    DockHooks::configure();

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

    impl.dialogs = std::make_unique<DialogService>(*this);
    impl.fileDialogs = std::make_unique<FileDialogService>(*this);
    impl.views = std::make_unique<ViewService>(*this);
    impl.launches = std::make_unique<ProcessLaunchService>(*this);

    // The actions are the core's children, made from the catalogue's table; what they do is connected when a shell
    // gives its targets.
    impl.catalogue = std::make_unique<ActionCatalogue>(*this);
    impl.bindings = std::make_unique<ActionBindings>(*impl.catalogue, *this);
    impl.dockLayout = std::make_unique<DockLayoutOperations>(*this);

    ActionStateCoordinator::Dependencies stateDeps;
    stateDeps.actions = impl.catalogue->lookup();
    stateDeps.subjectView = [&impl]() { return impl.views->subjectView(); };
    stateDeps.lookingView = [&impl]() { return impl.views->lookingView(); };
    stateDeps.registryActions = [&impl]() { return impl.processMenus->extensionActions(); };
    stateDeps.timerContext = this;
    stateDeps.isMaximizedViewActive = [&impl]() {
        return impl.shell != nullptr && impl.shell->viewHost().isMaximizedViewActive();
    };
    stateDeps.iconProvider = [&impl](const std::string& key) {
        return impl.iconManager ? impl.iconManager->getIcon(QString::fromUtf8(key)) : QIcon();
    };
    impl.actionState = std::make_unique<ActionStateCoordinator>(std::move(stateDeps));

    ProcessMenuModel::Dependencies menuDeps;
    menuDeps.recentProcesses = &impl.recentProcesses;
    menuDeps.favoriteProcesses = &impl.favoriteProcesses;
    menuDeps.actions = impl.catalogue->lookup();
    menuDeps.launch = [&impl](const std::string& descriptorId) { return impl.launches->launch(descriptorId); };
    menuDeps.manageExtensions = [&impl]() { impl.dialogs->openExtensionsAt(ExtensionService::statusPageTitle()); };
    menuDeps.bindExtensionAction = [&impl](QAction* action, const std::string& category,
                                           const std::string& descriptorId) {
        impl.bindings->bindRegistryAction(action, category, descriptorId);
    };
    impl.processMenus = std::make_unique<ProcessMenuModel>(std::move(menuDeps));

    impl.extensions = std::make_unique<ExtensionService>(*this);

    // A launch the menus record, and an extension's environment a launch prepared, change the process menus.
    ProcessLaunchService::Hooks launchHooks;
    launchHooks.recentProcessesChanged = [&impl]() {
        impl.processMenus->notifyChanged(ProcessMenuModel::Change::RecentProcesses);
    };
    launchHooks.extensionsPrepared = [&impl](const std::vector<an::ui::extensions::ExtensionLoadResult>& discovered) {
        impl.extensions->extensionsPrepared(discovered);
    };
    impl.launches->setHooks(std::move(launchHooks));
}

ApplicationCore::~ApplicationCore()
{
    m_impl->eventTarget = nullptr;
    m_impl->extensions.reset();
    m_impl->launches.reset();
    m_impl->shell = nullptr;
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

DialogService& ApplicationCore::dialogService() noexcept
{
    return *m_impl->dialogs;
}

FileDialogService& ApplicationCore::fileDialogService() noexcept
{
    return *m_impl->fileDialogs;
}

ViewService& ApplicationCore::viewService() noexcept
{
    return *m_impl->views;
}

ProcessLaunchService& ApplicationCore::processLaunch() noexcept
{
    return *m_impl->launches;
}

ExtensionService& ApplicationCore::extensions() noexcept
{
    return *m_impl->extensions;
}

const std::shared_ptr<IconManager>& ApplicationCore::iconManager() const noexcept
{
    return m_impl->iconManager;
}

ActionCatalogue& ApplicationCore::actions() noexcept
{
    return *m_impl->catalogue;
}

ActionBindings& ApplicationCore::actionBindings() noexcept
{
    return *m_impl->bindings;
}

ActionStateCoordinator& ApplicationCore::actionState() noexcept
{
    return *m_impl->actionState;
}

ProcessMenuModel& ApplicationCore::processMenus() noexcept
{
    return *m_impl->processMenus;
}

DockLayoutOperations& ApplicationCore::dockLayout() noexcept
{
    return *m_impl->dockLayout;
}

FileDropHandler& ApplicationCore::fileDrops() noexcept
{
    return m_impl->fileDrops;
}

void ApplicationCore::setEngineSessionProvider(an::ui::IEngineSessionProvider* provider) noexcept
{
    m_impl->session = provider;
}

an::ui::IEngineSessionProvider* ApplicationCore::engineSessionProvider() const noexcept
{
    return m_impl->session;
}

void ApplicationCore::setShell(IShell* shell)
{
    if (shell == m_impl->shell)
    {
        return;
    }
    // The preparations' windows belong to the shell's window.
    m_impl->launches->releasePreparations();
    m_impl->shell = shell;
}

IShell* ApplicationCore::shell() const noexcept
{
    return m_impl->shell;
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
