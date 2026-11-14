// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "common.h"
#include "app/AfterNightApp.h"
#include "app/ApplicationSession.h"
#include "app/AfterNightAppUiBuilder.h"
#include "app/ModernizationFeatureFlags.h"
#include "app/ExtensionManager.h"
#include "app/ExtensionEnvironmentManager.h"
#include "app/ExtensionHostManager.h"
#include "app/ExtensionLaunchPreparations.h"
#include "app/SharedBufferManager.h"
#include "app/ProcessRegistry.h"
#include "app/ProcessLauncher.h"
#include "app/ProcessMenuState.h"
#include "app/ChannelExtractor.h"
#include "app/ThemeManager.h"
#include "app/MenuActionManager.h"
#include "app/FileCompanions.h"
#include "app/FileManager.h"
#include "app/FileDialogUtils.h"
#include "app/ViewControlCoordinator.h"
#include "app/ViewCreationCoordinator.h"
#include "app/ImageOperationsCoordinator.h"
#include "app/MaskOverlayCoordinator.h"
#include "app/DialogOrchestrationCoordinator.h"
#include "app/RuntimeStorageCoordinator.h"
#include "app/UiBootstrapCoordinator.h"
#include "app/WindowLayoutCoordinator.h"
#include "app/AppLifecycleCoordinator.h"
#include "app/ExtensionCommon.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/ProgressDialogControl.h"
#include "controls/MainDashboard.h"
#include "controls/LogViewerControl.h"
#include "controls/settings/ExtensionStatusPage.h"
#include "controls/settings/ProcessingLimits.h"
#include "controls/ThumbnailPersistence.h"
#include "controls/FolderTrackerPersistence.h"
#include "controls/ProcessSettingsPersistence.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "widgets/ImageInfoStatusBarWidget.h"
#include "controls/ExportSequenceControl.h"
#include "controls/NewSequenceControl.h"
#include "controls/settings/SettingsKeys.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/KDDockWidgetsHelper.h"
#include "controls/ImageProcessControl.h"
#include "controls/ImageHistogramControl.h"
#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/DashboardUtils.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include "app/MessageBoxes.h"

#include "actions/ActionBindings.h"
#include "actions/ActionCatalogue.h"
#include "actions/ActionStateCoordinator.h"
#include "actions/ProcessMenuModel.h"
#include "actions/ViewMenuBuilder.h"
#include "core/ApplicationContext.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationEvents.h"
#include "services/AppEventBus.h"
#include "services/DialogService.h"
#include "services/ExtensionService.h"
#include "services/FavoriteProcessesModel.h"
#include "services/FileDialogService.h"
#include "services/FileDropHandler.h"
#include "services/OpenProcessRegistry.h"
#include "services/ProcessLaunchService.h"
#include "services/RecentFilesModel.h"
#include "services/RecentProcessesModel.h"
#include "services/WindowGeometryStore.h"
#include "views/DockLayoutOperations.h"
#include "views/ViewService.h"

#include <AfterNightPlatform/Text.h>
#include <AfterNightPlatform/Logging.h>

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/IO/SequenceSelection.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <kddockwidgets/Config.h>
#include "kddockwidgets/core/FloatingWindow.h"
#include "kddockwidgets/core/MainWindow.h"
#include "kddockwidgets/core/DropArea.h"
#include "kddockwidgets/core/DockWidget.h"

#include <QList>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QUuid>
#include <QUrl>
#include <QScreen>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <map>

namespace
{

using an::ui::ScopedTopLevelFlagRestorer;

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

} // anonymous namespace

struct AfterNightApp::Impl
{
    explicit Impl(an::ui::shell::ApplicationCore& core) : m_Core(core) {}

    // The application without a window, which `main` made before this window and which outlives it.
    an::ui::shell::ApplicationCore& m_Core;

    // What the core's marshalled events run against while this window lives: the core's services.
    std::unique_ptr<an::ui::shell::ServiceEventTarget> m_EventTarget;

    an::ui::app::AfterNightAppUiElements ui;

    // The application's list of views, the core's view service's.
    ImageViewControlManager* m_ImageViewControlManager = nullptr;
    std::unique_ptr<an::ui::ProcessLauncher> m_ProcessLauncher;
    std::unique_ptr<an::ui::ChannelExtractor> m_ChannelExtractor;
    std::unique_ptr<an::ui::ThemeManager> m_ThemeManager;
    std::unique_ptr<an::ui::MenuActionManager> m_MenuActionManager;
    std::unique_ptr<an::ui::FileManager> m_FileManager;
    std::unique_ptr<an::ui::ViewControlCoordinator> m_ViewControlCoordinator;
    std::unique_ptr<an::ui::ImageOperationsCoordinator> m_ImageOperationsCoordinator;
    std::unique_ptr<an::ui::MaskOverlayCoordinator> m_MaskOverlayCoordinator;
    std::unique_ptr<an::ui::DialogOrchestrationCoordinator> m_DialogOrchestrationCoordinator;
    std::unique_ptr<an::ui::UiBootstrapCoordinator> m_UiBootstrapCoordinator;
    std::unique_ptr<an::ui::WindowLayoutCoordinator> m_WindowLayoutCoordinator;
    std::unique_ptr<an::ui::AppLifecycleCoordinator> m_AppLifecycleCoordinator;
    std::unique_ptr<an::ui::shell::ApplicationContext> m_AppContext;

    // The session the caller of `initialize` owns and lends; it outlives this window.
    an::ui::ApplicationSession* m_Session = nullptr;

    std::unique_ptr<QComboBox> m_ImageViewControlListComboBox;
    std::unique_ptr<ImageInfoStatusBarWidget> m_ImageInfoStatusBarWidget;

    // The menus of the menu bar by their object names, which the core's action state names a menu by.
    QHash<QString, QMenu*> m_MenusByName;
    // What makes this window's process menus again when the core's process menus change.
    std::size_t m_ProcessMenusSubscription = 0;

    bool m_UnlockLayoutWhenRestoring = false;

    LogViewerControl* m_LogViewerControl = nullptr;
    // The console's destroyed handler, which reads this state: let go before the state goes, since the console is a
    // child the window's base class deletes after it.
    QMetaObject::Connection m_LogViewerClosedConnection;

    bool m_DarkMode = false;

    IconManagerSptr m_IconManager;

    QToolBar* m_DockManagerToolBar = nullptr;
    QToolButton* m_ToolbarLockLayout = nullptr;

    KDDockWidgets::QtWidgets::MainWindow* m_DockMainWindow = nullptr;

    MainDashboard* m_MainDashboard = nullptr;
    QScrollArea* m_MainDashboardScrollArea = nullptr;

    bool m_IsTerminating = false;

    int m_MaxThreadsForBatchProcess = 0;
    int m_MaxThreadsForPreviewProcess = 0;
};

// The window's state lives behind the pimpl; beside it the window holds only the tables of the three interfaces of the
// shell contract it implements.
static_assert(sizeof(AfterNightApp) <= sizeof(QMainWindow) + sizeof(std::uintptr_t) * 5,
              "AfterNightApp should stay thin: its state lives behind the pimpl.");

AfterNightApp* AfterNightApp::m_Instance = nullptr;
AfterNightApp* AfterNightApp::createInstance(an::ui::shell::ApplicationCore& core)
{
    if (!m_Instance)
    {
        m_Instance = new AfterNightApp(core);
    }

    return m_Instance;
}

AfterNightApp* AfterNightApp::getInstance()
{
    return m_Instance;
}

AfterNightApp* AfterNightApp::instanceOrNull() noexcept
{
    return m_Instance;
}

void AfterNightApp::destroyInstance()
{
    if (m_Instance)
    {
        delete m_Instance;
        m_Instance = nullptr;
    }
}

AfterNightApp::AfterNightApp(an::ui::shell::ApplicationCore& core)
    : QMainWindow(nullptr), m_impl(std::make_unique<Impl>(core))
{
    // The core's services answer through this window - where a view goes, what a dialog belongs to, what the status
    // line shows - until it goes; the work the threads hand the GUI thread runs against the services meanwhile.
    m_impl->m_ImageViewControlManager = &core.viewService().manager();
    core.setShell(this);
    an::ui::shell::ServiceEventTarget::Dependencies eventTargetDeps;
    eventTargetDeps.addRecentFile = [this](const std::string& fileName,
                                           const std::shared_ptr<const an::consumer::ImageAsset>& image) {
        addRecentFile(fileName, image);
    };
    eventTargetDeps.removeRecentFile = [this](const std::string& fileName) { removeRecentFile(fileName); };
    m_impl->m_EventTarget = std::make_unique<an::ui::shell::ServiceEventTarget>(core, std::move(eventTargetDeps));
    core.setEventTarget(m_impl->m_EventTarget.get());
    // Install the process-global app context before constructing UI services so
    // any control created during startup observes a live context. It is the core's and this shell's; what is still
    // this window's is handed to it.
    an::ui::shell::ApplicationContext::Dependencies contextDeps;
    contextDeps.applyTheme = [this](const QString& themeId) { applyTheme(themeId); };
    contextDeps.openRecentSingleFile = [this](const QString& filePath) { openRecentSingleFile(filePath); };
    contextDeps.openDroppedFiles = [this](const QStringList& files) { openDroppedFiles(files); };
    m_impl->m_AppContext = std::make_unique<an::ui::shell::ApplicationContext>(core, *this, std::move(contextDeps));
    an::ui::setAppContext(m_impl->m_AppContext.get());

    // The application's actions are the core's; the window holds them while it lives, as its own, and its menus, its
    // tool bar and its shortcuts are made of them.
    core.actions().host(*this);
    m_impl->ui = an::ui::app::buildAfterNightAppUi(this, core.actions().lookup());
    for (QMenu* menu : m_impl->ui.menuBar->findChildren<QMenu*>())
    {
        m_impl->m_MenusByName.insert(menu->objectName(), menu);
    }
    setObjectName("afterNightMainWindow");
    setMinimumSize(an::ui::kMainWindowMinimumWidth, an::ui::kMainWindowMinimumHeight);

    m_impl->m_IconManager = core.iconManager();
    m_impl->m_ProcessLauncher = std::make_unique<an::ui::ProcessLauncher>();
    m_impl->m_ChannelExtractor = std::make_unique<an::ui::ChannelExtractor>();
    an::ui::ThemeManager::Dependencies themeManagerDeps;
    themeManagerDeps.darkMode = &m_impl->m_DarkMode;
    themeManagerDeps.reloadIcons = [this](const an::ui::ThemeIconRefreshRequest& request) {
        if (!m_impl->m_IconManager)
        {
            return;
        }

        m_impl->m_IconManager->setColors(request.normalIconColor, request.disabledIconColor);
        m_impl->m_IconManager->setSemanticColors(request.infoIconColor, request.warningIconColor,
                                                 request.errorIconColor, request.questionIconColor,
                                                 request.successIconColor);
        m_impl->m_IconManager->loadIcons(":/AfterNight/icons/SVG");
        m_impl->m_IconManager->loadIcons(":/AfterNight/icons/PNG");
    };
    themeManagerDeps.setActionIcons = [this]() { setActionIcons(); };
    themeManagerDeps.mainDashboard = &m_impl->m_MainDashboard;
    themeManagerDeps.toolbarLockLayout = &m_impl->m_ToolbarLockLayout;
    themeManagerDeps.isLayoutLocked = [this]() { return layoutIsLocked(); };
    themeManagerDeps.imageViewControlManager = m_impl->m_ImageViewControlManager;
    themeManagerDeps.dockMainWindow = &m_impl->m_DockMainWindow;
    themeManagerDeps.forEachOpenProcess = [this](const std::function<void(TopLevelProcessBaseControl*)>& visit) {
        m_impl->m_Core.openProcesses().forEach(visit);
    };
    themeManagerDeps.mainWindow = this;
    themeManagerDeps.iconProvider = [this](const std::string& iconName) {
        return m_impl->m_IconManager ? m_impl->m_IconManager->getIcon(QString::fromUtf8(iconName)) : QIcon();
    };
    m_impl->m_ThemeManager = std::make_unique<an::ui::ThemeManager>(themeManagerDeps);

    // Create the image view channel combobox
    m_impl->m_ImageViewControlListComboBox = std::make_unique<QComboBox>();
    an::ui::MenuActionManager::Dependencies menuActionDeps;
    menuActionDeps.signalContext = this;
    menuActionDeps.imageSelector = m_impl->m_ImageViewControlListComboBox.get();
    menuActionDeps.onImageSelectorChanged = [this](const QString& text) {
        on_imageViewControlListComboBox_currentTextChanged(text);
    };
    m_impl->m_MenuActionManager = std::make_unique<an::ui::MenuActionManager>(menuActionDeps);

    // What the actions act on that is still this window's: its file workflows, image operations and mask overlay, its
    // dialogs and its dock area.
    an::ui::shell::ActionBindings::Targets targets;
    targets.openFile = [this]() {
        if (m_impl->m_FileManager)
        {
            m_impl->m_FileManager->openFile();
        }
    };
    targets.saveFile = [this]() {
        if (m_impl->m_FileManager)
        {
            m_impl->m_FileManager->saveFile();
        }
    };
    targets.newSequence = [this]() { newSequence(); };
    targets.exit = [this]() { close(); };
    targets.imageOperations = [this]() -> an::ui::ImageOperationsCoordinator* {
        return m_impl->m_ImageOperationsCoordinator.get();
    };
    targets.maskOverlay = [this]() -> an::ui::MaskOverlayCoordinator* {
        return m_impl->m_MaskOverlayCoordinator.get();
    };
    targets.launch = [this](const std::string& descriptorId) { return tryLaunchRegistryProcessById(descriptorId); };
    targets.sessionConsole = [this](const bool visible) { on_LogViewerClicked(visible); };
    targets.userGuide = [this]() { openHelp(); };
    targets.about = [this]() { about(); };
    targets.systemInformation = [this]() { systemInformation(); };
    targets.settings = [this]() { openSettings(); };
    targets.extensionManager = [this]() { openSettingsAt(an::ui::shell::ExtensionService::managerPageTitle()); };
    targets.lockLayout = [this]() { lockLayout(); };
    targets.unlockLayout = [this]() { unlockLayout(); };
    targets.groupAll = [this]() { groupAllImages(); };
    targets.splitAll = [this]() { splitAllImages(); };
    core.actionBindings().setTargets(std::move(targets));

    // The actions' state sets this window's menus, and stops while the window closes.
    an::ui::shell::ActionStateCoordinator::Hooks actionStateHooks;
    actionStateHooks.setMenuEnabled = [this](const std::string_view menuId, const bool enabled) {
        if (QMenu* const menu =
                m_impl->m_MenusByName.value(QString::fromUtf8(menuId.data(), static_cast<qsizetype>(menuId.size()))))
        {
            menu->setEnabled(enabled);
        }
    };
    actionStateHooks.isTerminating = [this]() { return m_impl->m_IsTerminating; };
    core.actionState().setHooks(std::move(actionStateHooks));

    // The process menus are made from the core's, again whenever they change.
    an::ui::shell::ProcessMenuModel::Hooks processMenuHooks;
    processMenuHooks.clearRecentProcesses = [this]() { clearRecentProcessesList(); };
    processMenuHooks.manageFavoriteProcesses = [this]() { openManageFavoriteProcessesDialog(); };
    processMenuHooks.clearFavoriteProcesses = [this]() { clearFavoriteProcessesList(); };
    core.processMenus().setHooks(std::move(processMenuHooks));
    m_impl->m_ProcessMenusSubscription =
        core.processMenus().subscribe([this](const an::ui::shell::ProcessMenuModel::Change change) {
            switch (change)
            {
            case an::ui::shell::ProcessMenuModel::Change::RecentProcesses:
                updateRecentProcessesMenuEntries();
                break;
            case an::ui::shell::ProcessMenuModel::Change::FavoriteProcesses:
                updateFavoriteProcessesMenuEntries();
                break;
            case an::ui::shell::ProcessMenuModel::Change::Extensions:
                updateExtensionMenuEntries();
                break;
            }
        });

    // The installed extensions join the menus - the ones the start-up catalogue names now - before the catalogue's
    // process actions are bound, so an extension's process bound to one of them keeps it.
    core.extensions().start();
    core.actionBindings().connectProcessActions();

    an::ui::FileManager::Dependencies fileManagerDeps;
    fileManagerDeps.signalContext = this;
    fileManagerDeps.recentFilesMenu = m_impl->ui.menuOpen_Recent;
    fileManagerDeps.recentFiles = &m_impl->m_Core.recentFiles().entriesForEdit();
    fileManagerDeps.lastOpenDirectory = &m_impl->m_Core.lastOpenDirectoryForEdit();
    fileManagerDeps.recentFilesLimit = [this]() { return m_impl->m_Core.recentFiles().limit(); };
    fileManagerDeps.saveRecentFiles = [this]() { m_impl->m_Core.recentFiles().save(); };
    fileManagerDeps.reloadRecentFilesThumbnails = [this]() {
        if (m_impl->m_MainDashboard)
        {
            m_impl->m_MainDashboard->reloadRecentFilesThumbnails();
        }
    };
    fileManagerDeps.notifyRecentFilesListDeleted = [this]() {
        if (m_impl->m_MainDashboard)
        {
            m_impl->m_MainDashboard->notifyRecentFilesListDeleted();
        }
    };
    fileManagerDeps.rawOpenPolicy = [this]() { return rawOpenPolicy(); };
    fileManagerDeps.persistLastOpenDirectory = [this](const QString& path) { setLastOpenDirectoryAndPersist(path); };
    fileManagerDeps.switchToDockManager = [this]() { switchToDockManager(); };
    fileManagerDeps.thumbnailsDirPath = [this]() { return getThumbnailsDirPath(); };
    fileManagerDeps.updateImageViewControlList = [this]() { updateImageViewControlList(); };
    // A file the person opens is shown in a view of theirs.
    fileManagerDeps.viewFactory = &m_impl->m_Core.viewService().userViews();
    m_impl->m_FileManager = std::make_unique<an::ui::FileManager>(fileManagerDeps);
    an::ui::ViewControlCoordinator::Dependencies viewControlDeps;
    viewControlDeps.imageViewControlManager = m_impl->m_ImageViewControlManager;
    viewControlDeps.imageViewControlListComboBox = m_impl->m_ImageViewControlListComboBox.get();
    viewControlDeps.mainDashboard = &m_impl->m_MainDashboard;
    viewControlDeps.switchToMainDashboard = [this]() { switchToMainDashboard(); };
    viewControlDeps.getDockWidgetsByViewControlMap = [this]() { return getDockWidgetsByViewControlMap(); };
    m_impl->m_ViewControlCoordinator = std::make_unique<an::ui::ViewControlCoordinator>(viewControlDeps);
    an::ui::ImageOperationsCoordinator::Dependencies imageOperationsDeps;
    imageOperationsDeps.activeViewControl = [this]() -> ImageViewControl* {
        return m_impl->m_ImageViewControlManager ? m_impl->m_ImageViewControlManager->getActiveImageViewControl()
                                                 : nullptr;
    };
    imageOperationsDeps.listViewControls = [this]() -> std::vector<ImageViewControl*> {
        return m_impl->m_ImageViewControlManager ? m_impl->m_ImageViewControlManager->getListOfImageViewControls()
                                                 : std::vector<ImageViewControl*>();
    };
    imageOperationsDeps.messageParent = [this]() -> QWidget* { return this; };
    imageOperationsDeps.notifyErrorMessage = [this](const QString& title, const QString& text) {
        notifyErrorMessage(this, title, text);
    };
    // The image menu's actions that open no window: the processes library's table, which the registry's own image
    // operations run too.
    imageOperationsDeps.nonInteractive = an::ui::nonInteractiveImageOperations();
    // The image menu's Export opens the export window over the sequence the view shows.
    imageOperationsDeps.exportSequence = [](ImageViewControl* control) {
        if (control != nullptr)
        {
            (void)ExportSequenceControl::openFor(*control);
        }
    };
    m_impl->m_ImageOperationsCoordinator = std::make_unique<an::ui::ImageOperationsCoordinator>(imageOperationsDeps);
    an::ui::MaskOverlayCoordinator::Dependencies maskOverlayDeps;
    maskOverlayDeps.actions = core.actions().lookup();
    maskOverlayDeps.imageViewControlManager = m_impl->m_ImageViewControlManager;
    maskOverlayDeps.notifyErrorMessage = [this](const QString& title, const QString& text) {
        notifyErrorMessage(this, title, text);
    };
    m_impl->m_MaskOverlayCoordinator = std::make_unique<an::ui::MaskOverlayCoordinator>(maskOverlayDeps);
    an::ui::DialogOrchestrationCoordinator::Dependencies dialogDeps;
    dialogDeps.mainWindow = this;
    dialogDeps.darkMode = &m_impl->m_DarkMode;
    dialogDeps.extensionBackgroundWorkCancellationToken = core.extensions().cancellationToken();
    dialogDeps.globalSettingsPersistence = [this]() { return m_impl->m_Core.globalSettingsStore(); };
    dialogDeps.recentFiles = &m_impl->m_Core.recentFiles().entriesForEdit();
    dialogDeps.mainDashboard = &m_impl->m_MainDashboard;
    dialogDeps.getRecentFilesLimit = [this]() { return getRecentFilesLimit(); };
    dialogDeps.iconProvider = [this](const std::string& iconName) {
        return m_impl->m_IconManager ? m_impl->m_IconManager->getIcon(QString::fromUtf8(iconName)) : QIcon();
    };
    dialogDeps.removeRecentFileThumbnail = [this](const std::string& fileName) {
        if (m_impl->m_FileManager)
        {
            m_impl->m_FileManager->removeRecentFileThumbnail(fileName);
        }
    };
    dialogDeps.persistRecentFiles = [this]() { persistRecentFiles(); };
    dialogDeps.settingsApplied = [this]() { m_impl->m_Core.extensions().settingsApplied(); };
    dialogDeps.updateRecentFilesMenuEntries = [this]() {
        if (m_impl->m_FileManager)
        {
            m_impl->m_FileManager->updateRecentFilesMenuEntries();
        }
    };
    m_impl->m_DialogOrchestrationCoordinator = std::make_unique<an::ui::DialogOrchestrationCoordinator>(dialogDeps);
    an::ui::UiBootstrapCoordinator::Dependencies uiBootstrapDeps;
    uiBootstrapDeps.ui = &m_impl->ui;
    uiBootstrapDeps.mainWindow = this;
    uiBootstrapDeps.imageViewControlListComboBox = m_impl->m_ImageViewControlListComboBox.get();
    uiBootstrapDeps.imageInfoStatusBarWidget = &m_impl->m_ImageInfoStatusBarWidget;
    uiBootstrapDeps.mainDashboard = &m_impl->m_MainDashboard;
    uiBootstrapDeps.mainDashboardScrollArea = &m_impl->m_MainDashboardScrollArea;
    uiBootstrapDeps.dockMainWindow = &m_impl->m_DockMainWindow;
    uiBootstrapDeps.dockManagerToolBar = &m_impl->m_DockManagerToolBar;
    uiBootstrapDeps.toolbarLockLayout = &m_impl->m_ToolbarLockLayout;
    uiBootstrapDeps.connectActions = [this]() { connectActions(); };
    uiBootstrapDeps.refreshTheme = [this]() { refreshTheme(); };
    uiBootstrapDeps.onOpenFileRequested = [this]() {
        if (m_impl->m_FileManager)
        {
            m_impl->m_FileManager->openFile();
        }
    };
    uiBootstrapDeps.onNewSequenceRequested = [this]() { newSequence(); };
    uiBootstrapDeps.onImageIntegrationRequested = [this]() {
        if (!tryLaunchRegistryProcessById("multi_image_stacking.actionImage_Integration") && m_impl->m_ProcessLauncher)
        {
            m_impl->m_ProcessLauncher->executeImageIntegration();
        }
    };
    uiBootstrapDeps.onOpenExtensionManagerRequested = [this]() {
        openSettingsAt(an::ui::shell::ExtensionService::managerPageTitle());
    };
    uiBootstrapDeps.onClearRecentFilesRequested = [this]() {
        if (m_impl->m_FileManager)
        {
            m_impl->m_FileManager->clearRecentFilesList();
        }
    };
    uiBootstrapDeps.onOpenSettingsRequested = [this]() { openSettings(); };
    uiBootstrapDeps.onSystemInformationRequested = [this]() { systemInformation(); };
    uiBootstrapDeps.onAboutRequested = [this]() { about(); };
    uiBootstrapDeps.onOpenHelpRequested = [this]() { openHelp(); };
    uiBootstrapDeps.onOpenRecentFileRequested = [this](const QString& filePath) {
        if (m_impl->m_FileManager)
        {
            m_impl->m_FileManager->openRecentSingleFile(filePath);
        }
    };
    uiBootstrapDeps.onRemoveRecentFileRequested = [this](const QString& filePath) {
        removeRecentFile(filePath.toStdString());
    };
    m_impl->m_UiBootstrapCoordinator = std::make_unique<an::ui::UiBootstrapCoordinator>(uiBootstrapDeps);
    an::ui::WindowLayoutCoordinator::Dependencies windowLayoutDeps;
    windowLayoutDeps.ui = &m_impl->ui;
    windowLayoutDeps.imageViewControlManager = m_impl->m_ImageViewControlManager;
    windowLayoutDeps.imageViewControlListComboBox = m_impl->m_ImageViewControlListComboBox.get();
    windowLayoutDeps.timerContext = this;
    windowLayoutDeps.mainDashboard = &m_impl->m_MainDashboard;
    windowLayoutDeps.mainDashboardScrollArea = &m_impl->m_MainDashboardScrollArea;
    windowLayoutDeps.dockMainWindow = &m_impl->m_DockMainWindow;
    windowLayoutDeps.dockManagerToolBar = &m_impl->m_DockManagerToolBar;
    windowLayoutDeps.toolbarLockLayout = &m_impl->m_ToolbarLockLayout;
    windowLayoutDeps.unlockLayoutWhenRestoring = &m_impl->m_UnlockLayoutWhenRestoring;
    windowLayoutDeps.iconProvider = [this](const std::string& iconName) {
        return m_impl->m_IconManager ? m_impl->m_IconManager->getIcon(QString::fromUtf8(iconName)) : QIcon();
    };
    windowLayoutDeps.setActiveImageViewControl = [this](ImageViewControl* control) {
        setActiveImageViewControl(control);
    };
    windowLayoutDeps.addToolBar = [this](QToolBar* toolBar) { addToolBar(toolBar); };
    windowLayoutDeps.toggleLayout = [this]() { toogleLayout(); };
    windowLayoutDeps.lockLayout = [this]() { lockLayout(); };
    windowLayoutDeps.unlockLayout = [this]() { unlockLayout(); };
    windowLayoutDeps.layoutIsLocked = [this]() { return layoutIsLocked(); };
    windowLayoutDeps.viewsSharingTheWindowOf = [this](ImageViewControl& view) {
        return an::ui::shell::DockLayoutOperations::viewsSharingTheWindowOf(
            view, m_impl->m_Core.viewService().getListOfViewControls());
    };
    m_impl->m_WindowLayoutCoordinator = std::make_unique<an::ui::WindowLayoutCoordinator>(windowLayoutDeps);
    an::ui::AppLifecycleCoordinator::Dependencies appLifecycleDeps;
    appLifecycleDeps.mainWindow = this;
    appLifecycleDeps.globalSettingsPersistence = [this]() { return m_impl->m_Core.globalSettingsStore(); };
    appLifecycleDeps.imageViewControlManager = m_impl->m_ImageViewControlManager;
    appLifecycleDeps.isTerminating = &m_impl->m_IsTerminating;
    appLifecycleDeps.maxThreadsForBatchProcess = &m_impl->m_MaxThreadsForBatchProcess;
    appLifecycleDeps.maxThreadsForPreviewProcess = &m_impl->m_MaxThreadsForPreviewProcess;
    appLifecycleDeps.logModernizationFeatureFlags = []() { logModernizationFeatureFlags(); };
    appLifecycleDeps.initializePersistence = [this]() { initializePersistence(); };
    appLifecycleDeps.cleanTemporaryFiles = [this]() { cleanTemporaryFiles(); };
    appLifecycleDeps.cleanTemporaryFilesAsync = [this]() { cleanTemporaryFilesAsync(); };
    appLifecycleDeps.cleanStaleScratchRootsAsync = [this]() { cleanStaleScratchRootsAsync(); };
    appLifecycleDeps.applyTheme = [this](const QString& themeId) { applyTheme(themeId); };
    appLifecycleDeps.loadIcons = [this]() { loadIcons(); };
    appLifecycleDeps.setActionIcons = [this]() { setActionIcons(); };
    appLifecycleDeps.refreshProxyStyleTokens = []() {
        if (auto* proxyStyle = an::ui::installedAnProxyStyle())
        {
            proxyStyle->refreshThemeTokens();
        }
    };
    appLifecycleDeps.createAuxToolBars = [this]() { createAuxToolBars(); };
    appLifecycleDeps.setEnableImageActions = [this](const bool enable) { setEnableImageActions(enable); };
    appLifecycleDeps.updateRecentFilesMenuEntries = [this]() {
        if (m_impl->m_FileManager)
        {
            m_impl->m_FileManager->updateRecentFilesMenuEntries();
        }
    };
    appLifecycleDeps.initializeMainDashboard = [this]() { initializeMainDashboard(); };
    appLifecycleDeps.initializeDockManager = [this]() { initializeDockManager(); };
    appLifecycleDeps.initializeMaximizedView = [this]() { initializeMaximizedView(); };
    appLifecycleDeps.lockLayout = [this]() { lockLayout(); };
    appLifecycleDeps.cancelBackgroundWork = [this]() { m_impl->m_Core.extensions().cancelBackgroundWork(); };
    appLifecycleDeps.notifyApplicationClose = [this]() { notifyApplicationClose(); };
    appLifecycleDeps.getNumOfOpenProcesses = [this]() { return getNumOfOpenProcesses(); };
    appLifecycleDeps.closeOpenProcesses = [this]() { stopExtensionHostSessions(); };
    appLifecycleDeps.messageBoxYesNo = [this](QWidget* parent, const QString& title, const QString& text,
                                              const bool warn,
                                              int* ret) { messageBoxYesNo(parent, title, text, warn, ret); };
    appLifecycleDeps.showWindowWhereItWas = [this](QWidget& window) {
        an::ui::shell::WindowGeometryStore(m_impl->m_Core).show(window);
    };
    appLifecycleDeps.keepWindowPlace = [this](const QWidget& window) {
        an::ui::shell::WindowGeometryStore(m_impl->m_Core).save(window);
    };
    m_impl->m_AppLifecycleCoordinator = std::make_unique<an::ui::AppLifecycleCoordinator>(appLifecycleDeps);

    // What the core's services ask of this window that is not the shell's to answer: the image selector, the context
    // menu, the settings dialogs and an extension's questions.
    an::ui::shell::ViewService::Hooks viewHooks;
    viewHooks.refreshViewList = [this]() { updateImageViewControlList(); };
    viewHooks.viewUpdated = [this](const ImageViewControl* view) {
        if (m_impl->m_ViewControlCoordinator)
        {
            m_impl->m_ViewControlCoordinator->viewUpdated(view);
        }
    };
    viewHooks.contextMenu = [this](ImageViewControl* view, QContextMenuEvent* event) {
        createContextMenuForViewControl(view, event);
    };
    viewHooks.isTerminating = [this]() { return m_impl->m_IsTerminating; };
    core.viewService().setHooks(std::move(viewHooks));

    an::ui::shell::DialogService::Hooks dialogHooks;
    dialogHooks.openSettingsAt = [this](const QString& pageName) { openSettingsAt(pageName); };
    dialogHooks.openExtensionsAt = [this](const QString& pageName) { openSettingsAt(pageName); };
    core.dialogService().setHooks(std::move(dialogHooks));

    // Files dropped on this window are opened by its file manager.
    core.fileDrops().setHooks({.openFiles = [this](const QStringList& files) { openDroppedFiles(files); }});

    an::ui::shell::ExtensionService::Hooks extensionHooks;
    extensionHooks.askYesNo = [this](const QString& title, const QString& text, const bool warn,
                                     std::function<void(int)> answered) {
        if (m_impl->m_DialogOrchestrationCoordinator)
        {
            m_impl->m_DialogOrchestrationCoordinator->openMessageBoxYesNo(this, title, text, warn, std::move(answered));
        }
    };
    extensionHooks.openExtensionSettings = [this](const std::string_view extensionId, QString* diagnostic) {
        return m_impl->m_DialogOrchestrationCoordinator &&
               m_impl->m_DialogOrchestrationCoordinator->openExtensionSettingsDialog(extensionId, diagnostic);
    };
    core.extensions().setHooks(std::move(extensionHooks));

    if (m_impl->m_UiBootstrapCoordinator)
    {
        m_impl->m_UiBootstrapCoordinator->finalizeConstructorUiSetup();
    }
}

void AfterNightApp::initialize(an::ui::ApplicationSession& session)
{
    // The session is opened once the settings store has been read, from the limits it states, before anything that
    // reads or shows an image.
    m_impl->m_Session = &session;

    if (m_impl->m_AppLifecycleCoordinator)
    {
        m_impl->m_AppLifecycleCoordinator->initialize();
    }

    // The stores are open: the SDK the extensions' sessions talk to is told what it reads of the application.
    m_impl->m_Core.extensions().configureSdk();

    refreshStoredProcessMenus();
}

void AfterNightApp::setActiveImageViewControl(ImageViewControl* view)
{
    m_impl->m_Core.viewService().setActiveImageViewControl(view);
}

void AfterNightApp::setActiveImageViewControlAsync(ImageViewControl* view, std::atomic_bool* finished)
{
    m_impl->m_Core.setActiveImageViewControlAsync(view, finished);
}

void AfterNightApp::setActiveImageViewControlByName(const std::string& name)
{
    m_impl->m_Core.viewService().setActiveViewByName(name);
}

void AfterNightApp::setActiveImageViewControlByNameAsync(const std::string& name, std::atomic_bool* finished)
{
    if (ImageViewControl* const view = m_impl->m_ImageViewControlManager->getImageViewControlByName(name))
    {
        setActiveImageViewControlAsync(view, finished);
    }
}

void AfterNightApp::addImageViewControl(ImageViewControl* view)
{
    m_impl->m_Core.viewService().addViewControl(view);
}

void AfterNightApp::addImageViewControlAsync(ImageViewControl* view)
{
    m_impl->m_Core.addImageViewControlAsync(view);
}

void AfterNightApp::removeImageViewControl(ImageViewControl* view, bool deleteObject)
{
    m_impl->m_Core.viewService().removeViewControl(view, deleteObject);
}

void AfterNightApp::removeImageViewControlAsync(ImageViewControl* view, bool deleteObject)
{
    m_impl->m_Core.removeImageViewControlAsync(view, deleteObject);
}

void AfterNightApp::renameImageViewControl(ImageViewControl* view)
{
    m_impl->m_Core.viewService().renameViewControl(view);
}

void AfterNightApp::updateImageViewControlList()
{
    if (m_impl->m_ViewControlCoordinator)
    {
        m_impl->m_ViewControlCoordinator->updateImageViewControlList();
    }
}

void AfterNightApp::updateImageViewControlListAsync(std::atomic_bool* finished)
{
    m_impl->m_Core.updateImageViewControlListAsync(finished);
}

ImageViewControlManager* AfterNightApp::getImageViewControlManager()
{
    return m_impl->m_ImageViewControlManager;
}

an::ui::ProcessLauncher* AfterNightApp::processLauncher() const
{
    return m_impl->m_ProcessLauncher.get();
}

an::ui::ChannelExtractor* AfterNightApp::channelExtractor() const
{
    return m_impl->m_ChannelExtractor.get();
}

an::ui::FileManager* AfterNightApp::fileManager() const
{
    return m_impl->m_FileManager.get();
}

an::ui::ImageOperationsCoordinator* AfterNightApp::imageOperations() const
{
    return m_impl->m_ImageOperationsCoordinator.get();
}

an::ui::MaskOverlayCoordinator* AfterNightApp::maskOverlay() const
{
    return m_impl->m_MaskOverlayCoordinator.get();
}

an::ui::WindowLayoutCoordinator* AfterNightApp::windowLayout() const
{
    return m_impl->m_WindowLayoutCoordinator.get();
}

an::ui::DialogOrchestrationCoordinator* AfterNightApp::dialogOrchestration() const
{
    return m_impl->m_DialogOrchestrationCoordinator.get();
}

void AfterNightApp::createContextMenuForViewControl(ImageViewControl* view, QContextMenuEvent* event)
{
    if (view == nullptr || event == nullptr)
    {
        return;
    }

    // The view's menu is the shell core's, over the menus of this window's menu bar.
    QMenu menu(view);
    an::ui::shell::ViewMenuBuilder(m_impl->m_Core).buildViewMenu(menu, *view, [this](std::string_view menuId) {
        return m_impl->m_MenusByName.value(QString::fromLatin1(menuId.data(), static_cast<qsizetype>(menuId.size())));
    });
    menu.exec(event->globalPos());
}

ImageViewControl*
AfterNightApp::createNewImageViewControlFromImage(ImageViewControl* customViewControl, an::ui::ImageDocument&& document,
                                                  QWidget* parent, const std::string& historyDesc,
                                                  const ViewControlMode mode, ImageViewControl* neighbour,
                                                  bool keepFocusedNeighbour, const bool pendingChanges)
{
    return m_impl->m_Core.viewService().createNewImageViewControlFromImage(customViewControl, std::move(document),
                                                                           parent, historyDesc, mode, neighbour,
                                                                           keepFocusedNeighbour, pendingChanges);
}

ImageViewControl* AfterNightApp::createNewImageViewControlFromSequence(
    ImageViewControl* customViewControl, an::ui::ImageDocument&& document, QWidget* parent, const bool autoDebayer,
    const std::string& historyDesc, const ViewControlMode mode, ImageViewControl* neighbour, bool keepFocusedNeighbour,
    const bool pendingChanges)
{
    return m_impl->m_Core.viewService().createNewImageViewControlFromSequence(
        customViewControl, std::move(document), parent, autoDebayer, historyDesc, mode, neighbour, keepFocusedNeighbour,
        pendingChanges);
}

void AfterNightApp::createNewImageViewControlFromImageAsync(ImageViewControl* customViewControl,
                                                            an::ui::ImageDocument&& document, QWidget* parent,
                                                            const std::string& historyDesc, const ViewControlMode mode,
                                                            ImageViewControl* neighbour, bool keepFocusedNeighbour,
                                                            const bool pendingChanges, std::atomic_bool* finished)
{
    m_impl->m_Core.createNewImageViewControlFromImageAsync(customViewControl, std::move(document), parent, historyDesc,
                                                           mode, neighbour, keepFocusedNeighbour, pendingChanges,
                                                           finished);
}

void AfterNightApp::createNewImageViewControlFromSequenceAsync(ImageViewControl* customViewControl,
                                                               an::ui::ImageDocument&& document, QWidget* parent,
                                                               const bool autoDebayer, const std::string& historyDesc,
                                                               const ViewControlMode mode, ImageViewControl* neighbour,
                                                               bool keepFocusedNeighbour, const bool pendingChanges,
                                                               std::atomic_bool* finished)
{
    m_impl->m_Core.createNewImageViewControlFromSequenceAsync(customViewControl, std::move(document), parent,
                                                              autoDebayer, historyDesc, mode, neighbour,
                                                              keepFocusedNeighbour, pendingChanges, finished);
}

void AfterNightApp::registerProcessInstance(TopLevelProcessBaseControl* process)
{
    m_impl->m_Core.openProcesses().add(process);
}

void AfterNightApp::unregisterProcessInstance(TopLevelProcessBaseControl* process)
{
    m_impl->m_Core.openProcesses().remove(process);
}

size_t AfterNightApp::getNumOfOpenProcesses()
{
    return m_impl->m_Core.openProcesses().size() + m_impl->m_Core.extensions().activeSessionCount();
}

void AfterNightApp::stopExtensionHostSessions()
{
    m_impl->m_Core.extensions().stopSessions();
}

KDDockWidgets::QtWidgets::MainWindow* AfterNightApp::getDockMainWindow()
{
    return m_impl->m_DockMainWindow;
}

ThumbnailPersistence* AfterNightApp::getThumbnailPersistence()
{
    return m_impl->m_Core.thumbnailStore();
}

FolderTrackerPersistence* AfterNightApp::getFolderTrackerPersistence()
{
    return m_impl->m_Core.folderStore();
}

ProcessSettingsPersistence* AfterNightApp::getProcessSettingsPersistence()
{
    return m_impl->m_Core.processSettingsStore();
}

GlobalSettingsPersistence* AfterNightApp::getGlobalSettingsPersistence()
{
    return m_impl->m_Core.globalSettingsStore();
}

bool AfterNightApp::isMaximizedViewActive() const
{
    return m_impl->ui.centralWidget->currentWidget() != m_impl->m_MainDashboardScrollArea &&
           m_impl->ui.centralWidget->currentWidget() != m_impl->m_DockMainWindow;
}

AfterNightApp::~AfterNightApp()
{
    // A Session Console still open is a child the base class deletes once this window's state is gone, so its
    // destroyed handler, which reads that state, must not run then.
    disconnect(m_impl->m_LogViewerClosedConnection);
    m_impl->m_Core.extensions().cancelBackgroundWork();
    stopExtensionHostSessions();

    // The process windows go before the stores they read. A run's work calls its window's process body on a worker, so
    // no window is deleted under its work: each stops what it still runs, the thread pool is drained while every window
    // is still there, only then are they deleted, and the work their going leaves on the pool - a window's read of its
    // settings among it - is drained in turn, while the settings stores are still there.
    const std::vector<TopLevelProcessBaseControl*> open = m_impl->m_Core.openProcesses().snapshot();
    std::vector<QPointer<TopLevelProcessBaseControl>> openProcesses(open.begin(), open.end());
    an::ui::destroyProcessWindowsAtExit(openProcesses);

    // The stores go after the windows that read them, in their order.
    m_impl->m_Core.closeStores();

    // The work handed to this window and not run yet goes with it, and nothing handed over later asks anything of it.
    m_impl->m_Core.discardPendingEvents();
    m_impl->m_Core.setEventTarget(nullptr);

    // The context goes with this window, and the windows destroyed after it - its children - must find none rather
    // than one on its way out.
    if (an::ui::appContext() == m_impl->m_AppContext.get())
    {
        an::ui::clearAppContext();
    }

    // The services answer through this window no more: they are given no hooks into it and no shell, and the views
    // still listed go with it - its children, deleted after this, find no context to leave the list through.
    an::ui::shell::ViewService& views = m_impl->m_Core.viewService();
    views.setHooks({});
    views.clearTaskSubject();
    views.manager().clear();
    m_impl->m_Core.dialogService().setHooks({});
    m_impl->m_Core.fileDrops().setHooks({});
    m_impl->m_Core.extensions().setHooks({});
    m_impl->m_Core.actionState().setHooks({});
    m_impl->m_Core.processMenus().setHooks({});
    m_impl->m_Core.processMenus().unsubscribe(m_impl->m_ProcessMenusSubscription);
    m_impl->m_Core.actionBindings().setTargets({});
    // The actions are the core's: the window gives them back before its base class deletes its children.
    m_impl->m_Core.actions().release();
    m_impl->m_Core.setShell(nullptr);
    m_impl->m_Core.setEngineSessionProvider(nullptr);
    m_impl->m_Session = nullptr;
}

an::ui::IEngineSessionProvider* AfterNightApp::engineSessionProvider() const noexcept
{
    return m_impl->m_Session != nullptr && m_impl->m_Session->isOpen() ? m_impl->m_Session : nullptr;
}

void AfterNightApp::on_imageViewControlListComboBox_currentTextChanged(const QString& text)
{
    auto control = m_impl->m_ImageViewControlManager->getImageViewControlByName(text.toUtf8().toStdString());

    if (control)
    {
        if (viewControlInAMaximizedContainer(control) && !control->isMaximized() && !control->isInFloatingWindow())
        {
            control->switchMaximized();
        }

        setActiveImageViewControl(control);
    }
}

void AfterNightApp::closeEvent(QCloseEvent* closeEvent)
{
    if (m_impl->m_AppLifecycleCoordinator && !m_impl->m_AppLifecycleCoordinator->prepareForClose())
    {
        closeEvent->ignore();
        return;
    }

    QMainWindow::closeEvent(closeEvent);

    // Some child dialogs (Histogram, Statistics, FITS viewer, etc.) are shown
    // as independent top-level windows and would keep the process alive if they
    // outlive the main window.  Quitting the application here ensures the
    // event loop always exits once the main window has accepted its close event.
    QApplication::quit();
}

void AfterNightApp::contextMenuEvent(QContextMenuEvent* contextMenuEvent)
{
    if (m_impl->m_ImageViewControlManager->getNumOfImageViewControls() == 0)
        return;

    QMenu menu(this);
    menu.addAction(m_impl->ui.actionLock_Layout);
    menu.addAction(m_impl->ui.actionUnlock_Layout);
    menu.exec(contextMenuEvent->globalPos());
}

void AfterNightApp::dragEnterEvent(QDragEnterEvent* evt)
{
    if (evt != nullptr && evt->mimeData() != nullptr && an::ui::shell::FileDropHandler::takes(*evt->mimeData()))
    {
        evt->acceptProposedAction();
    }
}

void AfterNightApp::dropEvent(QDropEvent* evt)
{
    if (evt != nullptr && evt->mimeData() != nullptr && m_impl->m_Core.fileDrops().drop(*evt->mimeData()))
    {
        evt->acceptProposedAction();
    }
}

void AfterNightApp::openDroppedFiles(const QStringList& files)
{
    if (m_impl->m_FileManager)
    {
        m_impl->m_FileManager->openDroppedFiles(files);
    }
}

bool AfterNightApp::tryLaunchRegistryProcessById(const std::string& processId)
{
    return m_impl->m_Core.processLaunch().launch(processId);
}

void AfterNightApp::connectActions()
{
    m_impl->m_Core.actionBindings().connectProcessActions();
    m_impl->m_Core.actionBindings().connectActions();
    if (m_impl->m_MenuActionManager)
    {
        m_impl->m_MenuActionManager->connectActions();
    }
}

void AfterNightApp::setActionIcons()
{
    const IconManagerSptr icons = m_impl->m_IconManager;
    if (!icons)
    {
        return;
    }
    m_impl->m_Core.actionBindings().setActionIcons([&icons](const QString& key) { return icons->getIcon(key); });
}

void AfterNightApp::initializePersistence()
{
    m_impl->m_Core.openStores();
    if (m_impl->m_FileManager)
    {
        m_impl->m_FileManager->setPersistenceDependencies(m_impl->m_Core.thumbnailStore(), m_impl->m_Core.folderStore(),
                                                          m_impl->m_Core.globalSettingsStore());
    }
    // A settings database of another format was set aside: the person is told where, once the window is up.
    if (const QString notice = m_impl->m_Core.settingsStoreNotice(); !notice.isEmpty())
    {
        notifyInfoMessageAsync(this, tr("Settings"), notice);
    }

    if (m_impl->m_Session != nullptr && !m_impl->m_Session->isOpen())
    {
        GlobalSettingsPersistence* const settings = m_impl->m_Core.globalSettingsStore();
        const an::ui::ProcessingLimits limits =
            settings != nullptr ? an::ui::readProcessingLimits([settings](const QString& key, const QString& fallback) {
                return settings->select(key, fallback);
            })
                                : an::ui::ProcessingLimits{};
        const an::Status opened = m_impl->m_Session->open(limits);
        if (opened.failed())
        {
            const std::string message = fmt::format("The engine session cannot be opened: {}", opened.message());
            LOG_ERROR(message);
            throw std::runtime_error(message);
        }
    }

    // The core lends the session the context hands out once it is open.
    m_impl->m_Core.setEngineSessionProvider(engineSessionProvider());
}

void AfterNightApp::persistLastOpenDir()
{
    m_impl->m_Core.persistLastOpenDirectory();
}

void AfterNightApp::persistRecentFiles()
{
    m_impl->m_Core.recentFiles().save();
}

std::shared_ptr<const an::ui::RawOpenPolicy> AfterNightApp::rawOpenPolicy() const
{
    return m_impl->m_Core.rawOpenPolicy();
}

an::Status AfterNightApp::setRawOpenSettings(const an::ui::RawOpenSettings& settings)
{
    return m_impl->m_Core.setRawOpenSettings(settings);
}

an::Result<an::OutputIntent> AfterNightApp::buildOutputIntent(const an::ImageDescriptor& source,
                                                              const an::CodecFormat container) const
{
    return m_impl->m_Core.buildOutputIntent(source, container);
}

void AfterNightApp::addRecentFile(const std::string& fileName,
                                  const std::shared_ptr<const an::consumer::ImageAsset>& image)
{
    if (m_impl->m_FileManager)
    {
        m_impl->m_FileManager->addRecentFile(fileName, image);
    }
}

void AfterNightApp::addRecentFileAsync(const std::string& fileName,
                                       std::shared_ptr<const an::consumer::ImageAsset> image,
                                       std::atomic_bool* finished)
{
    m_impl->m_Core.addRecentFileAsync(fileName, std::move(image), finished);
}

void AfterNightApp::removeRecentFile(const std::string& fileName)
{
    if (m_impl->m_FileManager)
    {
        m_impl->m_FileManager->removeRecentFile(fileName);
    }
}

void AfterNightApp::removeRecentFileAsync(const std::string& fileName, std::atomic_bool* finished)
{
    m_impl->m_Core.removeRecentFileAsync(fileName, finished);
}

void AfterNightApp::setLogViewerChecked(bool checked)
{
    QSignalBlocker blocker(m_impl->ui.actionLog_Viewer);
    m_impl->ui.actionLog_Viewer->setChecked(checked);
}

void AfterNightApp::onLogViewerClosed()
{
    m_impl->m_LogViewerControl = nullptr;

    // If no image is active, switch to main dashboard
    if (m_impl->m_ImageViewControlManager->getNumOfImageViewControls() == 0)
    {
        switchToMainDashboard();
    }
}

std::thread::id AfterNightApp::getMainThreadId() const
{
    return m_impl->m_Core.guiThreadId();
}

bool AfterNightApp::isMainThread() const
{
    return m_impl->m_Core.isGuiThread();
}

bool AfterNightApp::isTempFile(const std::string& fileName)
{
    return m_impl->m_Core.isTempFile(fileName);
}

void AfterNightApp::loadSettings()
{
    if (m_impl->m_AppLifecycleCoordinator)
    {
        m_impl->m_AppLifecycleCoordinator->loadSettings();
    }
}

void AfterNightApp::updateActiveImageActions()
{
    m_impl->m_Core.actionState().updateActiveImageActions();
}

void AfterNightApp::updateActiveImageActionsAsync()
{
    m_impl->m_Core.updateActiveImageActionsAsync();
}

void AfterNightApp::updateActiveImageHistory()
{
    m_impl->m_Core.actionState().updateActiveImageHistory();
}

void AfterNightApp::setCustomImageActionEnabled(ImageViewControl* view, const GlobalViewControlActions action,
                                                bool enable)
{
    m_impl->m_Core.actionState().setCustomImageActionEnabled(view, action, enable);
}

void AfterNightApp::updateImageInfoInStatusBar()
{
    showImageInfo(m_impl->m_ImageViewControlManager->getActiveImageViewControl());
}

void AfterNightApp::updateImageZoomInfoInStatusBar()
{
    showZoom(m_impl->m_ImageViewControlManager->getActiveImageViewControl());
}

void AfterNightApp::updateImageCelestialCoordsInStatusBar(const double ra_deg, const double dec_deg)
{
    showCelestialCoordinates(ra_deg, dec_deg);
}

void AfterNightApp::clearImageCelestialCoordsInStatusBar()
{
    clearCelestialCoordinates();
}

void AfterNightApp::subscribeListener(AfterNightAppListener* listener)
{
    m_impl->m_Core.eventBus().subscribe(listener);
}

void AfterNightApp::unsubscribeListener(AfterNightAppListener* listener)
{
    m_impl->m_Core.eventBus().unsubscribe(listener);
}

void AfterNightApp::notifyViewControlUpdated(const ImageViewControl* ctrl)
{
    m_impl->m_Core.viewService().notifyViewControlUpdated(ctrl);
}

void AfterNightApp::notifyProcessOpened(const std::string& processName, const ImageViewControl* targetCtrl)
{
    m_impl->m_Core.eventBus().notifyProcessOpened(processName, targetCtrl);
}

void AfterNightApp::notifyProcessExecuted(const std::string& processName, const ImageViewControl* targetCtrl)
{
    m_impl->m_Core.eventBus().notifyProcessExecuted(processName, targetCtrl);
}

void AfterNightApp::notifyApplicationClose()
{
    m_impl->m_Core.eventBus().notifyApplicationClose();
}

void AfterNightApp::setLastOpenDirectoryAndPersist(const QString& path)
{
    m_impl->m_Core.setLastOpenDirectory(path);
}

QStringList AfterNightApp::selectImageFilesDlg(QWidget* parent, const QString& title, const QString& folderContext,
                                               bool supportMultipleFiles)
{
    return m_impl->m_Core.fileDialogService().selectImageFiles(parent, title, folderContext, supportMultipleFiles);
}

QStringList AfterNightApp::selectFilesDlg(QWidget* parent, const QString& title, const QString& folderContext,
                                          const QStringList& fileExtensions)
{
    return m_impl->m_Core.fileDialogService().selectFiles(parent, title, folderContext, fileExtensions);
}

QStringList AfterNightApp::saveFileDlg(QWidget* parent, const QString& title, const QString& defaultDir,
                                       const QString& folderContext, const QStringList& fileExtensions)
{
    return m_impl->m_Core.fileDialogService().saveFiles(parent, title, defaultDir, folderContext, fileExtensions);
}

void AfterNightApp::newSequence()
{
    NewSequenceControl* newSequenceCtrl = new NewSequenceControl(this);

    newSequenceCtrl->show();
}

void AfterNightApp::on_LogViewerClicked(bool checked)
{
    if (checked)
    {
        // If we're on the main dashboard, we need to switch to dock manager first
        // since the session console is a dock widget that belongs to the dock manager
        if (m_impl->ui.centralWidget->currentWidget() == m_impl->m_MainDashboardScrollArea)
        {
            // We need to create a dummy view control to switch to dock manager
            // This will be closed when the user returns to main dashboard
            // For now, just switch to dock manager which will enable proper docking
            switchToDockManager();
        }

        if (m_impl->m_LogViewerControl)
        {
            m_impl->m_LogViewerControl->show();
            m_impl->m_LogViewerControl->getDockWidget()->show();
            return;
        }

        m_impl->m_LogViewerControl = new LogViewerControl(this);

        m_impl->m_LogViewerClosedConnection = connect(m_impl->m_LogViewerControl, &QObject::destroyed, this, [this]() {
            setLogViewerChecked(false);
            onLogViewerClosed();
        });

        m_impl->m_LogViewerControl->initialize();

        auto dockWidget = m_impl->m_LogViewerControl->getDockWidget();

        // New dock area
        m_impl->m_DockMainWindow->addDockWidget(dockWidget, KDDockWidgets::Location_OnBottom);
    }
    else
    {
        if (!m_impl->m_LogViewerControl)
        {
            return;
        }

        m_impl->m_LogViewerControl->getDockWidget()->forceClose();
        m_impl->m_LogViewerControl->close();
        // Note: m_impl->m_LogViewerControl is cleared and dashboard switching is handled
        // by the destroyed callback above.
    }
}

void AfterNightApp::lockLayout()
{
    // The lock is the core's, on this window's one dock area: the whole layout, as it always was. The toolbar's button
    // is this window's.
    m_impl->m_Core.dockLayout().lock(dockTarget());
    m_impl->m_ToolbarLockLayout->setIcon(m_impl->m_IconManager->getIcon("unlock"));
    m_impl->m_ToolbarLockLayout->setToolTip("Unlock layout");
}

void AfterNightApp::unlockLayout()
{
    m_impl->m_Core.dockLayout().unlock(dockTarget());
    m_impl->m_ToolbarLockLayout->setIcon(m_impl->m_IconManager->getIcon("lock"));
    m_impl->m_ToolbarLockLayout->setToolTip("Lock layout");
}

void AfterNightApp::toogleLayout()
{
    if (m_impl->ui.actionUnlock_Layout->isEnabled())
    {
        unlockLayout();
    }
    else
    {
        lockLayout();
    }
}

bool AfterNightApp::layoutIsLocked() const
{
    return m_impl->m_Core.dockLayout().isLocked(
        an::ui::shell::DockTarget{.mainWindow = m_impl->m_DockMainWindow, .workspaceId = {}});
}

void AfterNightApp::about()
{
    if (m_impl->m_DialogOrchestrationCoordinator)
    {
        m_impl->m_DialogOrchestrationCoordinator->about();
    }
}

void AfterNightApp::openHelp()
{
    if (m_impl->m_DialogOrchestrationCoordinator)
    {
        m_impl->m_DialogOrchestrationCoordinator->openHelp();
    }
}

void AfterNightApp::openSettings()
{
    if (m_impl->m_DialogOrchestrationCoordinator)
    {
        m_impl->m_DialogOrchestrationCoordinator->openSettings();
        m_impl->m_Core.processMenus().refreshStoredLists();
    }
}

void AfterNightApp::openSettingsAt(const QString& pageName)
{
    if (m_impl->m_DialogOrchestrationCoordinator)
    {
        if (an::ui::shell::ExtensionService::isExtensionsPageTitle(pageName))
        {
            m_impl->m_DialogOrchestrationCoordinator->openExtensionsAt(pageName);
            m_impl->m_Core.extensions().refreshSoon();
            return;
        }

        m_impl->m_DialogOrchestrationCoordinator->openSettingsAt(pageName);
        m_impl->m_Core.processMenus().refreshStoredLists();
    }
}

void AfterNightApp::systemInformation()
{
    if (m_impl->m_DialogOrchestrationCoordinator)
    {
        m_impl->m_DialogOrchestrationCoordinator->systemInformation();
    }
}

void AfterNightApp::postAfterNightEvent(AfterNightEvent* evt, int priority)
{
    m_impl->m_Core.postEvent(evt, priority);
}

void AfterNightApp::setEnableImageActions(bool enable)
{
    m_impl->m_Core.actionState().setEnableImageActions(enable);
}

void AfterNightApp::loadIcons()
{
    if (m_impl->m_ThemeManager)
    {
        m_impl->m_ThemeManager->loadIcons();
    }
}

void AfterNightApp::initializeMainDashboard()
{
    if (m_impl->m_UiBootstrapCoordinator)
    {
        m_impl->m_UiBootstrapCoordinator->initializeMainDashboard();
    }
}

void AfterNightApp::initializeDockManager()
{
    if (m_impl->m_UiBootstrapCoordinator)
    {
        m_impl->m_UiBootstrapCoordinator->initializeDockManager();
    }
}

void AfterNightApp::initializeMaximizedView()
{
    if (m_impl->m_UiBootstrapCoordinator)
    {
        m_impl->m_UiBootstrapCoordinator->initializeMaximizedView();
    }
}

void AfterNightApp::switchToMainDashboard()
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->switchToMainDashboard();
    }
}

void AfterNightApp::switchToDockManager()
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->switchToDockManager();
    }
}

std::deque<std::string> AfterNightApp::getRecentFilesList() const
{
    return m_impl->m_Core.recentFiles().entries();
}

void AfterNightApp::refreshStoredProcessMenus()
{
    m_impl->m_Core.processMenus().refreshStoredLists();
}

size_t AfterNightApp::getRecentFilesLimit() const
{
    return m_impl->m_Core.recentFiles().limit();
}

void AfterNightApp::updateRecentProcessesMenuEntries()
{
    if (m_impl->ui.menuRecent_Processes)
    {
        m_impl->m_Core.processMenus().buildRecentProcessesMenu(*m_impl->ui.menuRecent_Processes);
    }
}

void AfterNightApp::updateExtensionMenuEntries()
{
    const an::ui::shell::ProcessMenuModel& menus = m_impl->m_Core.processMenus();
    for (const an::ui::shell::ProcessMenuModel::Category& category : an::ui::shell::ProcessMenuModel::categories())
    {
        if (QMenu* const menu = m_impl->m_MenusByName.value(
                QString::fromUtf8(category.menuId.data(), static_cast<qsizetype>(category.menuId.size()))))
        {
            menus.appendExtensionProcesses(*menu, category.menuId);
        }
    }
    if (m_impl->ui.menuExtensions)
    {
        menus.buildExtensionFamilyMenus(*m_impl->ui.menuExtensions);
    }
}

void AfterNightApp::clearRecentProcessesList()
{
    m_impl->m_Core.recentProcesses().load();
    if (m_impl->m_Core.recentProcesses().entries().empty())
    {
        m_impl->m_Core.recentProcesses().save();
        updateRecentProcessesMenuEntries();
        return;
    }

    ScopedTopLevelFlagRestorer _;

    QMessageBox msgBox;
    msgBox.setText(tr("Are you sure you want to delete the recent used processes?"));
    msgBox.setInformativeText(tr("The history of used processes will be lost"));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);

    if (msgBox.exec() != QMessageBox::Yes)
    {
        return;
    }

    m_impl->m_Core.recentProcesses().clear();
    updateRecentProcessesMenuEntries();
}

void AfterNightApp::updateFavoriteProcessesMenuEntries()
{
    if (m_impl->ui.menuFavorite_Processes)
    {
        m_impl->m_Core.processMenus().buildFavoriteProcessesMenu(*m_impl->ui.menuFavorite_Processes);
    }
}

void AfterNightApp::openManageFavoriteProcessesDialog()
{
    m_impl->m_Core.favoriteProcesses().load();

    std::vector<std::pair<QString, std::string>> allProcesses;
    const auto& descriptors = an::ui::process_registry::ProcessRegistry::instance().all();
    allProcesses.reserve(descriptors.size());
    for (const auto& descriptor : descriptors)
    {
        if (descriptor.id.empty())
        {
            continue;
        }

        const QString displayName = an::ui::processDisplayNameFromDescriptor(&descriptor, descriptor.id);
        allProcesses.emplace_back(displayName, descriptor.id);
    }

    std::sort(allProcesses.begin(), allProcesses.end(), [](const auto& lhs, const auto& rhs) {
        return QString::compare(lhs.first, rhs.first, Qt::CaseInsensitive) < 0;
    });

    ScopedTopLevelFlagRestorer _;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Manage Favorite Processes"));
    dialog.resize(540, 640);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* searchInput = new QLineEdit(&dialog);
    searchInput->setPlaceholderText(tr("Search processes..."));
    layout->addWidget(searchInput);

    auto* processList = new QListWidget(&dialog);
    processList->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(processList);

    for (const auto& process : allProcesses)
    {
        const QString& displayName = process.first;
        const std::string& processId = process.second;

        auto* item = new QListWidgetItem(displayName, processList);
        item->setData(Qt::UserRole, QString::fromUtf8(processId));
        item->setToolTip(QString::fromUtf8(processId));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_impl->m_Core.favoriteProcesses().ids().contains(processId) ? Qt::CheckState::Checked
                                                                                         : Qt::CheckState::Unchecked);
    }

    connect(searchInput, &QLineEdit::textChanged, &dialog, [processList](const QString& text) {
        const QString normalizedText = text.trimmed();
        for (int i = 0; i < processList->count(); ++i)
        {
            QListWidgetItem* item = processList->item(i);
            if (!item)
            {
                continue;
            }

            const bool matches = normalizedText.isEmpty() ||
                                 item->text().contains(normalizedText, Qt::CaseInsensitive) ||
                                 item->toolTip().contains(normalizedText, Qt::CaseInsensitive);
            item->setHidden(!matches);
        }
    });

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    std::set<std::string> selectedFavoriteIds;
    for (int i = 0; i < processList->count(); ++i)
    {
        QListWidgetItem* item = processList->item(i);
        if (!item || item->checkState() != Qt::CheckState::Checked)
        {
            continue;
        }

        const QString processId = item->data(Qt::UserRole).toString();
        if (!processId.isEmpty())
        {
            selectedFavoriteIds.insert(processId.toUtf8().toStdString());
        }
    }

    m_impl->m_Core.favoriteProcesses().setIds(std::move(selectedFavoriteIds));
    updateFavoriteProcessesMenuEntries();
}

void AfterNightApp::clearFavoriteProcessesList()
{
    m_impl->m_Core.favoriteProcesses().load();
    if (m_impl->m_Core.favoriteProcesses().ids().empty())
    {
        updateFavoriteProcessesMenuEntries();
        return;
    }

    ScopedTopLevelFlagRestorer _;

    QMessageBox msgBox;
    msgBox.setText(tr("Are you sure you want to delete the favorite processes list?"));
    msgBox.setInformativeText(tr("The list of favorite processes will be lost"));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);

    if (msgBox.exec() != QMessageBox::Yes)
    {
        return;
    }

    m_impl->m_Core.favoriteProcesses().clear();
    updateFavoriteProcessesMenuEntries();
}

void AfterNightApp::openRecentSingleFile(const QString& filePath)
{
    if (m_impl->m_FileManager)
    {
        m_impl->m_FileManager->openRecentSingleFile(filePath);
    }
}

void AfterNightApp::createAuxToolBars()
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->createAuxToolBars();
    }
}

void AfterNightApp::splitAllImages()
{
    m_impl->m_Core.dockLayout().splitAllEqually(dockTarget());
}

void AfterNightApp::groupAllImages()
{
    m_impl->m_Core.dockLayout().groupAll(dockTarget());
}

bool AfterNightApp::viewControlInAMaximizedContainer(ImageViewControl* control)
{
    return m_impl->m_WindowLayoutCoordinator
               ? m_impl->m_WindowLayoutCoordinator->viewControlInAMaximizedContainer(control)
               : false;
}

std::map<ImageViewControl*, KDDockWidgets::QtWidgets::DockWidget*> AfterNightApp::getDockWidgetsByViewControlMap()
{
    return m_impl->m_WindowLayoutCoordinator ? m_impl->m_WindowLayoutCoordinator->getDockWidgetsByViewControlMap()
                                             : std::map<ImageViewControl*, KDDockWidgets::QtWidgets::DockWidget*>();
}

void AfterNightApp::notifyErrorMessage(QWidget* parent, const QString& title, const QString& text)
{
    m_impl->m_Core.dialogService().notifyErrorMessage(parent, title, text);
}

void AfterNightApp::notifyErrorMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    m_impl->m_Core.notifyErrorMessageAsync(parent, title, text);
}

void AfterNightApp::notifyWarnMessage(QWidget* parent, const QString& title, const QString& text)
{
    m_impl->m_Core.dialogService().notifyWarnMessage(parent, title, text);
}

void AfterNightApp::notifyWarnMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    m_impl->m_Core.notifyWarnMessageAsync(parent, title, text);
}

void AfterNightApp::notifyInfoMessage(QWidget* parent, const QString& title, const QString& text)
{
    m_impl->m_Core.dialogService().notifyInfoMessage(parent, title, text);
}

void AfterNightApp::notifyInfoMessageAsync(QWidget* parent, const QString& title, const QString& text)
{
    m_impl->m_Core.notifyInfoMessageAsync(parent, title, text);
}

void AfterNightApp::messageBoxYesNo(QWidget* parent, const QString& title, const QString& text, bool warn, int* ret)
{
    m_impl->m_Core.dialogService().messageBoxYesNo(parent, title, text, warn, ret);
}

void AfterNightApp::messageBoxYesNoAsync(QWidget* parent, const QString& title, const QString& text, bool warn,
                                         int* ret, std::atomic_bool* finished)
{
    m_impl->m_Core.messageBoxYesNoAsync(parent, title, text, warn, ret, finished);
}

void AfterNightApp::runCustomCode(const std::function<void()>& func)
{
    if (func)
    {
        func();
    }
}

void AfterNightApp::runCustomCodeAsync(const std::function<void()>& func)
{
    m_impl->m_Core.runCustomCodeAsync(func);
}

void AfterNightApp::cleanTemporaryFiles()
{
    m_impl->m_Core.runtimeStorage().cleanTemporaryFiles();
}

void AfterNightApp::cleanTemporaryFilesAsync()
{
    m_impl->m_Core.runtimeStorage().cleanTemporaryFilesAsync();
}

void AfterNightApp::cleanStaleScratchRootsAsync()
{
    m_impl->m_Core.runtimeStorage().cleanStaleScratchRootsAsync();
}

void AfterNightApp::ensureTempDirCreated()
{
    m_impl->m_Core.runtimeStorage().ensureTempDirCreated();
}

QString AfterNightApp::getTempDirPath() const
{
    return m_impl->m_Core.runtimeStorage().getTempDirPath();
}

void AfterNightApp::ensureThumbnailsDirCreated()
{
    m_impl->m_Core.runtimeStorage().ensureThumbnailsDirCreated();
}

QString AfterNightApp::getThumbnailsDirPath() const
{
    return m_impl->m_Core.runtimeStorage().getThumbnailsDirPath();
}

std::string AfterNightApp::createTempFilePathForNewSequence(const an::SequenceFormat format) const
{
    return m_impl->m_Core.runtimeStorage().createTempFilePathForNewSequence(format);
}

bool AfterNightApp::isDarkMode() const
{
    return m_impl->m_DarkMode;
}

void AfterNightApp::applyTheme(const QString& themeId)
{
    if (m_impl->m_ThemeManager)
    {
        m_impl->m_ThemeManager->applyTheme(themeId);
    }
    m_impl->m_Core.extensions().themeChanged();
}

void AfterNightApp::refreshAllZoomSettings()
{
    if (m_impl->m_ImageViewControlManager)
    {
        for (auto* viewControl : m_impl->m_ImageViewControlManager->getListOfImageViewControls())
        {
            if (viewControl)
            {
                viewControl->refreshZoomSettings();
            }
        }
    }
}

void AfterNightApp::showWithSavedGeometry()
{
    if (m_impl->m_AppLifecycleCoordinator)
    {
        m_impl->m_AppLifecycleCoordinator->showWithSavedGeometry();
    }
    if (m_impl->m_DialogOrchestrationCoordinator)
    {
        m_impl->m_DialogOrchestrationCoordinator->checkExtensionUpdatesOnStartup();
    }
}

void AfterNightApp::saveWindowGeometry()
{
    if (m_impl->m_AppLifecycleCoordinator)
    {
        m_impl->m_AppLifecycleCoordinator->saveWindowGeometry();
    }
}

void AfterNightApp::maximizeViewControl(ImageViewControl* ctrl)
{
    m_impl->m_Core.viewService().maximizeViewControl(ctrl);
}

void AfterNightApp::restoreViewControl(ImageViewControl* ctrl)
{
    m_impl->m_Core.viewService().restoreViewControl(ctrl);
}

IconManagerSptr AfterNightApp::getIconManager()
{
    return m_impl->m_IconManager;
}

std::set<std::string> AfterNightApp::getListOfTemporaryDirs()
{
    return m_impl->m_Core.runtimeStorage().getListOfTemporaryDirs();
}

// ---- The shell

void AfterNightApp::build() {}

QWidget* AfterNightApp::mainWindow()
{
    return this;
}

an::ui::shell::IViewHost& AfterNightApp::viewHost()
{
    return *this;
}

an::ui::shell::IStatusDisplay& AfterNightApp::statusDisplay()
{
    return *this;
}

an::ui::IProcessPresenter* AfterNightApp::processPresenter()
{
    return nullptr;
}

void AfterNightApp::setSessionConsoleVisible(const bool visible)
{
    setLogViewerChecked(visible);
    on_LogViewerClicked(visible);
}

bool AfterNightApp::isSessionConsoleVisible() const
{
    return m_impl->m_LogViewerControl != nullptr;
}

void AfterNightApp::refreshTheme()
{
    if (m_impl->m_ThemeManager)
    {
        m_impl->m_ThemeManager->refreshTheme();
    }
}

void AfterNightApp::saveGeometry()
{
    saveWindowGeometry();
}

// ---- Where the views are

void AfterNightApp::place(ImageViewControl& view, const an::ui::shell::ViewPlacement& placement)
{
    QWidget* const current = m_impl->ui.centralWidget ? m_impl->ui.centralWidget->currentWidget() : nullptr;
    const bool onDashboard = current == m_impl->m_MainDashboardScrollArea;
    const bool onDockArea = current == m_impl->m_DockMainWindow;
    if (current && !onDashboard && !onDockArea && m_impl->m_WindowLayoutCoordinator)
    {
        // A view maximized on its page goes back to the dock area, which the new view joins.
        for (ImageViewControl* maximized : m_impl->m_WindowLayoutCoordinator->getListOfMaximizedViewControls())
        {
            if (maximized != &view && !maximized->isInFloatingWindow())
            {
                maximized->switchMaximized();
            }
        }
    }

    an::ui::dockCreatedView(view, placement.neighbour, m_impl->m_DockMainWindow);
    showViews();
}

void AfterNightApp::activate(ImageViewControl& view)
{
    if (m_impl->m_ViewControlCoordinator)
    {
        m_impl->m_ViewControlCoordinator->viewActivated(&view);
    }
}

void AfterNightApp::viewAdded(ImageViewControl& view, const an::ui::shell::ViewOrigin /*origin*/)
{
    if (m_impl->m_ViewControlCoordinator)
    {
        m_impl->m_ViewControlCoordinator->viewAdded(&view);
    }
}

void AfterNightApp::viewRemoved(ImageViewControl& view)
{
    if (m_impl->m_ViewControlCoordinator)
    {
        m_impl->m_ViewControlCoordinator->viewRemoved(&view);
    }
}

void AfterNightApp::viewRenamed(ImageViewControl& view, const std::string& previousName)
{
    if (m_impl->m_ViewControlCoordinator)
    {
        m_impl->m_ViewControlCoordinator->viewRenamed(&view, previousName);
    }
}

void AfterNightApp::maximize(ImageViewControl& view)
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->maximizeViewControl(&view);
    }
}

void AfterNightApp::restore(ImageViewControl& view)
{
    if (m_impl->m_WindowLayoutCoordinator)
    {
        m_impl->m_WindowLayoutCoordinator->restoreViewControl(&view);
    }
}

an::ui::shell::DockTarget AfterNightApp::dockTarget(const ImageViewControl* /*view*/)
{
    return an::ui::shell::DockTarget{.mainWindow = m_impl->m_DockMainWindow, .workspaceId = {}};
}

void AfterNightApp::showHome()
{
    // The Session Console is a dock of the dock area, which stays on screen while it is open.
    if (m_impl->m_LogViewerControl == nullptr)
    {
        switchToMainDashboard();
    }
}

void AfterNightApp::showViews()
{
    if (m_impl->m_DockMainWindow && m_impl->ui.centralWidget &&
        m_impl->ui.centralWidget->currentWidget() != m_impl->m_DockMainWindow)
    {
        switchToDockManager();
    }
}

// ---- The status line

void AfterNightApp::showImageInfo(ImageViewControl* view)
{
    if (m_impl->m_ImageInfoStatusBarWidget)
    {
        m_impl->m_ImageInfoStatusBarWidget->updateImageInfo(view);
    }
}

void AfterNightApp::showZoom(ImageViewControl* view)
{
    if (m_impl->m_ImageInfoStatusBarWidget)
    {
        m_impl->m_ImageInfoStatusBarWidget->updateImageZoomInfo(view);
    }
}

void AfterNightApp::showCelestialCoordinates(const double raDegrees, const double decDegrees)
{
    if (m_impl->m_ImageInfoStatusBarWidget)
    {
        m_impl->m_ImageInfoStatusBarWidget->updateCelestialCoords(raDegrees, decDegrees);
    }
}

void AfterNightApp::clearCelestialCoordinates()
{
    if (m_impl->m_ImageInfoStatusBarWidget)
    {
        m_impl->m_ImageInfoStatusBarWidget->clearCelestialCoords();
    }
}

void AfterNightApp::clearImageInfo()
{
    showImageInfo(nullptr);
}
