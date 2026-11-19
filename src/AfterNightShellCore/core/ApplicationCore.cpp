// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "core/ApplicationCore.h"

#include "actions/ActionBindings.h"
#include "actions/ActionCatalogue.h"
#include "actions/ActionStateCoordinator.h"
#include "actions/ProcessMenuModel.h"
#include "commands/CommandIndex.h"
#include "commands/CommandProviders.h"
#include "core/ApplicationContext.h"
#include "core/ApplicationSession.h"
#include "core/IShell.h"
#include "projects/JobProjects.h"
#include "projects/ProjectHistoryBinder.h"
#include "projects/ProjectService.h"
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

#include "app/ExtensionHostManager.h"
#include "app/IconManager.h"
#include "app/ModernizationFeatureFlags.h"

#include "app/PersistenceCoordinator.h"
#include "app/RuntimeStorageCoordinator.h"
#include "app/ThemeManager.h"
#include "controls/FolderTrackerPersistence.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/IAppContext.h"
#include "controls/PersistenceDbExecutor.h"
#include "controls/ProcessSettingsPersistence.h"
#include "controls/ThumbnailPersistence.h"
#include "controls/ImageViewControl.h"
#include "controls/settings/ProcessingLimits.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnimationUtils.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <AfterNightPlatform/Text.h>

#include <QUuid>

#include <fmt/format.h>

#include <algorithm>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

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
    // The open project: declared after the settings it keeps the recent projects in, so it goes before them.
    ProjectService projects;

    // What keeps the open project's histories in its folder: after the service it reads, so it goes first.
    ProjectHistoryBinder projectHistory{projects};

    // What answers a job about projects: after the service it asks, so it goes first.
    JobProjects jobProjects{projects};

    IApplicationEventTarget* eventTarget = nullptr;
    IShell* shell = nullptr;
    an::ui::IEngineSessionProvider* session = nullptr;
    an::ui::ApplicationSession* applicationSession = nullptr;
    const ShellRegistry* shellRegistry = nullptr;

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
    an::ui::extensions::IExtensionSessionFeed* extensionSessions = nullptr;
    std::unique_ptr<CommandIndex> commands;

    // The context the application reaches the core through, made as the application starts and gone with the core.
    std::unique_ptr<ApplicationContext> context;
    bool initialized = false;
    bool started = false;
};

ApplicationCore::ApplicationCore() : QObject(nullptr), m_impl(std::make_unique<Impl>())
{
    // The dock library takes its configuration only before the first dock widget or main window exists: it is given
    // here, before any shell makes one.
    DockHooks::configure();

    Impl& impl = *m_impl;
    impl.recentProcesses.setSettings(&impl.settings);
    impl.favoriteProcesses.setSettings(&impl.settings);
    impl.projects.setSettings(&impl.settings);
    impl.jobProjects.setSettings(&impl.settings);

    an::ui::RuntimeStorageCoordinator::Dependencies runtimeStorageDeps;
    runtimeStorageDeps.localDataPath = &impl.localDataPath;
    runtimeStorageDeps.globalSettingsPersistence = [&impl]() { return impl.globalSettingsPersistence.get(); };
    runtimeStorageDeps.createUuidToken = []() { return QUuid::createUuid().toString().toUtf8().toStdString(); };
    impl.runtimeStorage = std::make_unique<an::ui::RuntimeStorageCoordinator>(runtimeStorageDeps);
    // A file of the session's temporary folder is no project's image.
    impl.projectHistory.setTemporaryTest([&impl](const QString& filePath) {
        const QString temporary = QDir::cleanPath(impl.runtimeStorage->getTempDirPath());
        return !temporary.isEmpty() &&
               QDir::cleanPath(filePath).startsWith(temporary + QLatin1Char('/'), Qt::CaseInsensitive);
    });

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
    stateDeps.taskShown = [&impl]() { return impl.views->hasTaskSubject(); };
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

    makeCommands();
}

void ApplicationCore::makeCommands()
{
    Impl& impl = *m_impl;
    impl.commands = std::make_unique<CommandIndex>();
    const QString recentKey = QString::fromLatin1(SettingsKeys::INTERFACE_PALETTE_RECENT_COMMANDS);
    impl.commands->setStore(CommandIndex::RecentStore{
        [&impl, recentKey]() { return impl.settings.select(recentKey); },
        [&impl, recentKey](const QString& ids) { (void)impl.settings.upsert(recentKey, ids); }});

    namespace registry = an::ui::process_registry;
    ProcessCommands::Dependencies processes;
    processes.imageGuards = [&impl](const registry::ProcessDescriptor& descriptor) {
        // With no image the process runs, and its launch says there is none, as the menu's does.
        ImageViewControl* const view = impl.views->subjectView();
        return view == nullptr ? registry::LaunchGuardResult{}
                               : registry::evaluateSingleImageLaunchGuards(
                                     descriptor, registry::launchGuardInputFor(descriptor, view));
    };
    processes.launch = [&impl](const std::string& descriptorId) { return impl.launches->launch(descriptorId); };
    processes.isFavorite = [&impl](const std::string& descriptorId) {
        return impl.favoriteProcesses.ids().count(descriptorId) != 0u;
    };
    processes.setFavorite = [&impl](const std::string& descriptorId, const bool favorite) {
        std::set<std::string> favourites = impl.favoriteProcesses.ids();
        if (favorite)
        {
            favourites.insert(descriptorId);
        }
        else
        {
            favourites.erase(descriptorId);
        }
        impl.favoriteProcesses.setIds(std::move(favourites));
        impl.favoriteProcesses.save();
        impl.processMenus->notifyChanged(ProcessMenuModel::Change::FavoriteProcesses);
    };
    processes.actions = impl.catalogue->lookup();
    processes.extensionFamily = [&impl](const std::string& extensionId) {
        return impl.processMenus->familyOf(extensionId);
    };
    impl.commands->addProvider(std::make_unique<ProcessCommands>(std::move(processes)));

    ImageCommands::Dependencies images;
    images.views = [&impl]() {
        std::vector<ImageCommands::OpenView> views;
        for (ImageViewControl* const view : impl.views->getListOfViewControls())
        {
            if (view != nullptr)
            {
                views.push_back(ImageCommands::OpenView{QString::fromStdString(view->getUniqueName()),
                                                        QString::fromStdString(view->getFileName())});
            }
        }
        return views;
    };
    images.activateView = [&impl](const QString& name) { impl.views->setActiveViewByName(name.toStdString()); };
    images.recentFiles = [&impl]() {
        std::vector<QString> files;
        for (const std::string& file : impl.recentFiles.entries())
        {
            files.push_back(QString::fromStdString(file));
        }
        return files;
    };
    images.openFile = [](const QString& path) {
        // A recent file opens as the shell's own recent files do.
        if (an::ui::IAppContext* const context = an::ui::appContext())
        {
            if (an::ui::IRecentFilesService* const files = context->getRecentFilesService())
            {
                files->openRecentSingleFile(path);
            }
        }
    };
    impl.commands->addProvider(std::make_unique<ImageCommands>(std::move(images)));

    ActionCommands::Dependencies actions;
    actions.actions = impl.catalogue->lookup();
    actions.launchesProcess = [](const std::string_view actionId) {
        const auto& all = registry::ProcessRegistry::instance().all();
        return std::any_of(all.begin(), all.end(), [actionId](const registry::ProcessDescriptor& descriptor) {
            return descriptor.action_object_name == actionId;
        });
    };
    impl.commands->addProvider(std::make_unique<ActionCommands>(std::move(actions)));

    SettingsCommands::Dependencies settings;
    settings.openPage = [&impl](const QString& pageTitle) { impl.dialogs->openSettingsAt(pageTitle); };
    impl.commands->addProvider(std::make_unique<SettingsCommands>(std::move(settings)));
}

ApplicationCore::~ApplicationCore()
{
    // The context goes first: nothing reaches the core through it once the core is on its way out.
    if (m_impl->context != nullptr && an::ui::appContext() == m_impl->context.get())
    {
        an::ui::clearAppContext();
    }
    m_impl->context.reset();
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
    // The commands the palette ran last live in the settings, which are open from here.
    m_impl->commands->loadRecents();
    // So do the projects used last: they are read again from the store that is open now.
    m_impl->projects.setSettings(&m_impl->settings);
    m_impl->jobProjects.setSettings(&m_impl->settings);
}

namespace
{

void logModernizationFeatureFlags()
{
    using an::ui::modernization::ModernizationFeatureFlags;

    LOG_DEBUG("Modernization feature flags: "
              "AN_UI_TOKENS_ENABLED={} (default={}), "
              "AN_UI_PROGRAMMATIC_PROCESSES={} (default={}), "
              "AN_DIALOG_MODERN_STYLE_ENABLED={} (default={}), "
              "AN_PERSISTENCE_THREADSAFE_ENABLED={} (default={})",
              ModernizationFeatureFlags::uiTokensEnabled(), ModernizationFeatureFlags::kUiTokensEnabledDefault,
              ModernizationFeatureFlags::programmaticProcessesEnabled(),
              ModernizationFeatureFlags::kProgrammaticProcessesEnabledDefault,
              ModernizationFeatureFlags::dialogModernStyleEnabled(),
              ModernizationFeatureFlags::kDialogModernStyleEnabledDefault,
              ModernizationFeatureFlags::persistenceThreadsafeEnabled(),
              ModernizationFeatureFlags::kPersistenceThreadsafeEnabledDefault);
}

/// Puts the interface's stored settings in force, in the order each depends on the last: a preset stored puts its three
/// parts in force over the ones stored beside it, and Night is off at every start unless the person asked to start in
/// it. The theme itself - system, light or dark - is the shell's window's to apply, once it exists.
void applyStoredInterfaceSettings(GlobalSettingsPersistence& settings)
{
    namespace keys = SettingsKeys;
    namespace defaults = SettingsDefaults;

    an::ui::setMaterialBackdropMode(an::ui::resolveMaterialBackdropMode(
        settings.select(keys::INTERFACE_MATERIAL_BACKDROP_MODE, QString(defaults::INTERFACE_MATERIAL_BACKDROP_MODE))));
    an::ui::setThemeContrastProfile(an::ui::resolveThemeContrastProfile(
        settings.select(keys::INTERFACE_THEME_CONTRAST_PROFILE, QString(defaults::INTERFACE_THEME_CONTRAST_PROFILE))));
    an::ui::setThemeAccentFamily(an::ui::resolveThemeAccentFamily(
        settings.select(keys::INTERFACE_THEME_ACCENT_FAMILY, QString(defaults::INTERFACE_THEME_ACCENT_FAMILY))));
    an::ui::setThemeSurfaceTintFamily(an::ui::resolveThemeSurfaceTintFamily(settings.select(
        keys::INTERFACE_THEME_SURFACE_TINT_FAMILY, QString(defaults::INTERFACE_THEME_SURFACE_TINT_FAMILY))));
    an::ui::setThemeTintStrength(an::ui::resolveThemeTintStrength(
        settings.select(keys::INTERFACE_THEME_TINT_STRENGTH, QString(defaults::INTERFACE_THEME_TINT_STRENGTH))));
    if (const std::optional<QString> preset = settings.selectOptional(keys::INTERFACE_THEME_PRESET); preset.has_value())
    {
        an::ui::applyThemePreset(an::ui::resolveThemePreset(*preset));
    }
    an::ui::setNightMode(settings.select(keys::INTERFACE_START_IN_NIGHT,
                                         defaults::INTERFACE_START_IN_NIGHT ? "1" : "0") == QStringLiteral("1"));
    an::ui::setThemeDensity(
        an::ui::resolveThemeDensity(settings.select(keys::INTERFACE_DENSITY, QString(defaults::INTERFACE_DENSITY))));
    an::ui::AnimationUtils::setAnimationsEnabled(
        settings.select(keys::INTERFACE_ANIMATIONS_ENABLED, defaults::INTERFACE_ANIMATIONS_ENABLED ? "1" : "0") ==
        QStringLiteral("1"));
}

} // namespace

void ApplicationCore::initialize()
{
    Impl& impl = *m_impl;
    if (impl.initialized)
    {
        return;
    }
    if (!storesOpen())
    {
        throw std::runtime_error("The application is initialized over its stores, and they are not open");
    }
    logModernizationFeatureFlags();

    // The context first: a control made from here on finds the application through it.
    impl.context = std::make_unique<ApplicationContext>(*this);
    an::ui::setAppContext(impl.context.get());

    // The session is opened once the settings store has been read, from the limits it states, before anything that
    // reads or shows an image.
    if (impl.applicationSession != nullptr && !impl.applicationSession->isOpen())
    {
        GlobalSettingsPersistence* const settings = impl.globalSettingsPersistence.get();
        const an::ui::ProcessingLimits limits =
            settings != nullptr ? an::ui::readProcessingLimits([settings](const QString& key, const QString& fallback) {
                return settings->select(key, fallback);
            })
                                : an::ui::ProcessingLimits{};
        const an::Status opened = impl.applicationSession->open(limits);
        if (opened.failed())
        {
            const std::string message = fmt::format("The engine session cannot be opened: {}", opened.message());
            LOG_ERROR(message);
            throw std::runtime_error(message);
        }
    }
    if (impl.applicationSession != nullptr && impl.applicationSession->isOpen())
    {
        // The core lends the session the context hands out once it is open.
        impl.session = impl.applicationSession;
    }

    if (GlobalSettingsPersistence* const settings = impl.globalSettingsPersistence.get())
    {
        applyStoredInterfaceSettings(*settings);
        // The temporary folder is cleared at the start as it is at the end, when the person asked for it.
        if (settings->select(SettingsKeys::GENERAL_CLEAR_TEMP_ON_EXIT, "1") == QStringLiteral("1"))
        {
            impl.runtimeStorage->cleanTemporaryFilesAsync();
        }
    }
    // Scratch stranded outside the managed temporary folder by an earlier crash or force-quit is never needed again,
    // so it is reclaimed whatever the preference says.
    impl.runtimeStorage->cleanStaleScratchRootsAsync();

    // The stores are open: the SDK the extensions' sessions talk to is told what it reads of the application.
    impl.extensions->configureSdk();
    impl.initialized = true;
}

bool ApplicationCore::isInitialized() const noexcept
{
    return m_impl->initialized;
}

void ApplicationCore::start()
{
    Impl& impl = *m_impl;
    if (impl.started)
    {
        return;
    }
    if (!impl.initialized)
    {
        throw std::runtime_error("The application starts once it is initialized");
    }
    // The installed extensions join the process menus - the ones the start-up catalogue names now - before a shell's
    // window binds the catalogue's process actions, so an extension's process bound to one of them keeps it.
    impl.extensions->start();
    impl.started = true;
}

bool ApplicationCore::isStarted() const noexcept
{
    return m_impl->started;
}

ApplicationContext* ApplicationCore::context() const noexcept
{
    return m_impl->context.get();
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

ProjectService& ApplicationCore::projects() noexcept
{
    return m_impl->projects;
}

ProjectHistoryBinder& ApplicationCore::projectHistory() noexcept
{
    return m_impl->projectHistory;
}

JobProjects& ApplicationCore::jobProjects() noexcept
{
    return m_impl->jobProjects;
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

an::ui::extensions::IExtensionSessionFeed& ApplicationCore::extensionSessions() noexcept
{
    if (m_impl->extensionSessions != nullptr)
    {
        return *m_impl->extensionSessions;
    }
    return m_impl->extensions->launchCoordinator();
}

void ApplicationCore::setExtensionSessions(an::ui::extensions::IExtensionSessionFeed* sessions) noexcept
{
    m_impl->extensionSessions = sessions;
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

CommandIndex& ApplicationCore::commands() noexcept
{
    return *m_impl->commands;
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

void ApplicationCore::setApplicationSession(an::ui::ApplicationSession* session) noexcept
{
    m_impl->applicationSession = session;
}

an::ui::ApplicationSession* ApplicationCore::applicationSession() const noexcept
{
    return m_impl->applicationSession;
}

void ApplicationCore::setShellRegistry(const ShellRegistry* registry) noexcept
{
    m_impl->shellRegistry = registry;
}

const ShellRegistry* ApplicationCore::shellRegistry() const noexcept
{
    return m_impl->shellRegistry;
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
