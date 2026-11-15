// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "ObservatoryWindow.h"

#include "chrome/StatusLine.h"
#include "chrome/TopBar.h"
#include "pages/HomePage.h"
#include "pages/WorkspacePage.h"
#include "workspaces/WorkspaceHost.h"

#include "actions/ActionBindings.h"
#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "actions/ActionStateCoordinator.h"
#include "actions/ProcessMenuModel.h"
#include "core/ApplicationContext.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationEvents.h"
#include "services/AppEventBus.h"
#include "services/DialogService.h"
#include "services/ExtensionService.h"
#include "services/FileDropHandler.h"
#include "services/OpenProcessRegistry.h"
#include "services/ProcessLaunchService.h"
#include "services/RecentFilesModel.h"
#include "services/WindowGeometryStore.h"
#include "views/DockLayoutOperations.h"
#include "views/ViewService.h"

#include "app/AppLifecycleCoordinator.h"
#include "app/ChannelExtractor.h"
#include "app/DialogOrchestrationCoordinator.h"
#include "app/FileManager.h"
#include "app/IconManager.h"
#include "app/ImageOperationsCoordinator.h"
#include "app/MaskOverlayCoordinator.h"
#include "app/ProcessLauncher.h"
#include "app/RuntimeStorageCoordinator.h"
#include "app/ThemeManager.h"
#include "app/ViewCreationCoordinator.h"
#include "components/AnIconRail.h"
#include "controls/ExportSequenceControl.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/LogViewerControl.h"
#include "controls/MainDashboard.h"
#include "controls/NewSequenceControl.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "theme/AnProxyStyle.h"

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/core/DockWidget.h>

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QPointer>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <functional>
#include <utility>
#include <vector>

namespace an::ui::observatory
{

namespace
{

/// The Image Integration process, which the dashboard's button opens.
constexpr char kImageIntegrationProcess[] = "multi_image_stacking.actionImage_Integration";

} // namespace

struct ObservatoryWindow::Impl
{
    explicit Impl(an::ui::shell::ApplicationCore& application) : core(application) {}

    // The application without a window, which outlives this one.
    an::ui::shell::ApplicationCore& core;

    // What the core's marshalled events run against while this window lives: the core's services.
    std::unique_ptr<an::ui::shell::ServiceEventTarget> eventTarget;

    // The regions.
    TopBar* topBar = nullptr;
    an::ui::AnIconRail* rail = nullptr;
    QStackedWidget* centre = nullptr;
    QWidget* rightColumn = nullptr;
    StatusLine* statusLine = nullptr;
    HomePage* homePage = nullptr;
    WorkspacePage* workspacePage = nullptr;
    std::unique_ptr<WorkspaceHost> workspaces;

    // What the window's units read through a pointer to a pointer: the dashboard, the dock area, the lock's button
    // (this window has none).
    MainDashboard* dashboard = nullptr;
    KDDockWidgets::QtWidgets::MainWindow* dockArea = nullptr;
    QToolButton* lockButton = nullptr;

    std::shared_ptr<IconManager> icons;
    std::unique_ptr<an::ui::ProcessLauncher> processLauncher;
    std::unique_ptr<an::ui::ChannelExtractor> channelExtractor;
    std::unique_ptr<an::ui::ThemeManager> themeManager;
    std::unique_ptr<an::ui::FileManager> fileManager;
    std::unique_ptr<an::ui::ImageOperationsCoordinator> imageOperations;
    std::unique_ptr<an::ui::MaskOverlayCoordinator> maskOverlay;
    std::unique_ptr<an::ui::DialogOrchestrationCoordinator> dialogs;
    std::unique_ptr<an::ui::AppLifecycleCoordinator> lifecycle;

    // The Session Console, a dock of the workspace's dock area while it is open.
    LogViewerControl* console = nullptr;
    QMetaObject::Connection consoleGone;

    // Whether the layout was locked by a maximize, and is unlocked again by the restore.
    bool unlockWhenRestored = false;
    bool darkMode = false;
    bool terminating = false;
    int batchThreads = 0;
    int previewThreads = 0;

    [[nodiscard]] QIcon icon(const std::string& name) const
    {
        return icons ? icons->getIcon(QString::fromUtf8(name)) : QIcon();
    }
};

ObservatoryWindow::ObservatoryWindow(an::ui::shell::ApplicationCore& core)
    : QMainWindow(nullptr), m_impl(std::make_unique<Impl>(core))
{
    Impl& impl = *m_impl;
    setObjectName(QStringLiteral("observatoryMainWindow"));
    setWindowTitle(QStringLiteral("AfterNight"));
    setMinimumSize(kMinimumWidth, kMinimumHeight);

    // The work the threads hand the GUI thread runs against the core's services while this window lives.
    an::ui::shell::ServiceEventTarget::Dependencies eventTargetDeps;
    eventTargetDeps.addRecentFile = [this](const std::string& fileName,
                                           const std::shared_ptr<const an::consumer::ImageAsset>& image) {
        if (m_impl->fileManager)
        {
            m_impl->fileManager->addRecentFile(fileName, image);
        }
    };
    eventTargetDeps.removeRecentFile = [this](const std::string& fileName) {
        if (m_impl->fileManager)
        {
            m_impl->fileManager->removeRecentFile(fileName);
        }
    };
    impl.eventTarget = std::make_unique<an::ui::shell::ServiceEventTarget>(core, std::move(eventTargetDeps));
    core.setEventTarget(impl.eventTarget.get());

    // The application context was made as the application started; what is this window's is handed to it.
    an::ui::shell::ApplicationContext::Dependencies contextDeps;
    contextDeps.applyTheme = [this](const QString& themeId) { applyThemeById(themeId); };
    contextDeps.openRecentSingleFile = [this](const QString& filePath) {
        if (m_impl->fileManager)
        {
            m_impl->fileManager->openRecentSingleFile(filePath);
        }
    };
    contextDeps.openDroppedFiles = [this](const QStringList& files) { openFiles(files); };
    core.context()->setDependencies(std::move(contextDeps));

    // The application's actions are the core's; the window holds them while it lives.
    core.actions().host(*this);
    impl.icons = core.iconManager();

    buildRegions();
    makeUnits();
    bindServices();

    // The installed extensions joined the process menus as the application started; the catalogue's actions are
    // connected to what they do, the process actions bound over the registry.
    core.actionBindings().connectProcessActions();
    core.actionBindings().connectActions();
}

ObservatoryWindow::~ObservatoryWindow()
{
    Impl& impl = *m_impl;
    an::ui::shell::ApplicationCore& core = impl.core;

    // A Session Console still open is a child the base class deletes once this window's state is gone, so its
    // destroyed handler, which reads that state, must not run then.
    disconnect(impl.consoleGone);
    core.extensions().cancelBackgroundWork();
    core.extensions().stopSessions();

    // The process windows go before the stores they read: each stops what it still runs, the thread pool is drained
    // while every window is still there, only then are they deleted.
    const std::vector<TopLevelProcessBaseControl*> open = core.openProcesses().snapshot();
    std::vector<QPointer<TopLevelProcessBaseControl>> openProcesses(open.begin(), open.end());
    an::ui::destroyProcessWindowsAtExit(openProcesses);

    // The stores go after the windows that read them.
    core.closeStores();

    // The work handed to this window and not run yet goes with it.
    core.discardPendingEvents();
    core.setEventTarget(nullptr);

    // The context is the core's: this window takes back what it handed it, and the windows destroyed after it - its
    // children - find no context.
    if (an::ui::shell::ApplicationContext* const context = core.context())
    {
        context->setDependencies({});
        if (an::ui::appContext() == context)
        {
            an::ui::clearAppContext();
        }
    }

    // The services answer through this window no more, and the views still listed go with it.
    an::ui::shell::ViewService& views = core.viewService();
    views.setHooks({});
    views.clearTaskSubject();
    views.manager().clear();
    core.dialogService().setHooks({});
    core.fileDrops().setHooks({});
    core.extensions().setHooks({});
    core.actionState().setHooks({});
    core.actionBindings().setTargets({});
    // The actions are the core's: the window gives them back before its base class deletes its children.
    core.actions().release();
    core.setEngineSessionProvider(nullptr);
}

void ObservatoryWindow::buildRegions()
{
    Impl& impl = *m_impl;
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("observatoryCentral"));
    auto* outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    impl.topBar = new TopBar(central);
    outer->addWidget(impl.topBar);

    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    outer->addLayout(body, 1);

    impl.rail = new an::ui::AnIconRail(central);
    impl.rail->setObjectName(QStringLiteral("observatoryRail"));
    body->addWidget(impl.rail);

    auto* right = new QVBoxLayout();
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(0);
    body->addLayout(right, 1);

    auto* middle = new QHBoxLayout();
    middle->setContentsMargins(0, 0, 0, 0);
    middle->setSpacing(0);
    right->addLayout(middle, 1);

    impl.centre = new QStackedWidget(central);
    impl.centre->setObjectName(QStringLiteral("observatoryCentre"));
    middle->addWidget(impl.centre, 1);

    impl.rightColumn = new QWidget(central);
    impl.rightColumn->setObjectName(QStringLiteral("observatoryRightColumn"));
    auto* columnLayout = new QVBoxLayout(impl.rightColumn);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(0);
    impl.rightColumn->hide();
    middle->addWidget(impl.rightColumn);

    impl.statusLine = new StatusLine(central);
    right->addWidget(impl.statusLine);

    setCentralWidget(central);

    // Home: the dashboard.
    impl.homePage = new HomePage(impl.centre);
    impl.dashboard = impl.homePage->dashboard();
    impl.centre->addWidget(impl.homePage);

    // The workspace: its dock area, made with this window as its parent and handed to its page.
    WorkspaceHost::Hooks workspaceHooks;
    workspaceHooks.arrangementChanged = [this]() { arrangementChanged(); };
    impl.workspaces = std::make_unique<WorkspaceHost>(*this, std::move(workspaceHooks));
    impl.dockArea = impl.workspaces->mainWindow();
    impl.workspacePage = new WorkspacePage(*impl.dockArea, impl.centre);
    impl.centre->addWidget(impl.workspacePage);

    impl.centre->setCurrentWidget(impl.homePage);
    connectDashboard();
}

void ObservatoryWindow::connectDashboard()
{
    MainDashboard* const dashboard = m_impl->dashboard;
    QObject::connect(dashboard, &MainDashboard::openFileRequested, this, [this]() { openFile(); });
    QObject::connect(dashboard, &MainDashboard::newSequenceRequested, this, [this]() { newSequence(); });
    QObject::connect(dashboard, &MainDashboard::imageIntegrationRequested, this, [this]() { openImageIntegration(); });
    QObject::connect(dashboard, &MainDashboard::openExtensionManagerRequested, this,
                     [this]() { openSettingsAt(an::ui::shell::ExtensionService::managerPageTitle()); });
    QObject::connect(dashboard, &MainDashboard::clearRecentFilesRequested, this, [this]() {
        if (m_impl->fileManager)
        {
            m_impl->fileManager->clearRecentFilesList();
        }
    });
    QObject::connect(dashboard, &MainDashboard::openSettingsRequested, this, [this]() { openSettings(); });
    QObject::connect(dashboard, &MainDashboard::systemInformationRequested, this, [this]() {
        if (m_impl->dialogs)
        {
            m_impl->dialogs->systemInformation();
        }
    });
    QObject::connect(dashboard, &MainDashboard::aboutRequested, this, [this]() {
        if (m_impl->dialogs)
        {
            m_impl->dialogs->about();
        }
    });
    QObject::connect(dashboard, &MainDashboard::openHelpRequested, this, [this]() {
        if (m_impl->dialogs)
        {
            m_impl->dialogs->openHelp();
        }
    });
    QObject::connect(dashboard, &MainDashboard::openRecentFileRequested, this, [this](const QString& filePath) {
        if (m_impl->fileManager)
        {
            m_impl->fileManager->openRecentSingleFile(filePath);
        }
    });
    QObject::connect(dashboard, &MainDashboard::removeRecentFileRequested, this, [this](const QString& filePath) {
        if (m_impl->fileManager)
        {
            m_impl->fileManager->removeRecentFile(filePath.toStdString());
        }
    });
}

void ObservatoryWindow::makeUnits()
{
    Impl& impl = *m_impl;
    an::ui::shell::ApplicationCore& core = impl.core;
    ImageViewControlManager* const viewManager = &core.viewService().manager();

    impl.processLauncher = std::make_unique<an::ui::ProcessLauncher>();
    impl.channelExtractor = std::make_unique<an::ui::ChannelExtractor>();

    an::ui::ThemeManager::Dependencies themeDeps;
    themeDeps.darkMode = &impl.darkMode;
    themeDeps.reloadIcons = [this](const an::ui::ThemeIconRefreshRequest& request) {
        const std::shared_ptr<IconManager>& icons = m_impl->icons;
        if (!icons)
        {
            return;
        }
        icons->setColors(request.normalIconColor, request.disabledIconColor);
        icons->setSemanticColors(request.infoIconColor, request.warningIconColor, request.errorIconColor,
                                 request.questionIconColor, request.successIconColor);
        icons->loadIcons(QStringLiteral(":/AfterNight/icons/SVG"));
        icons->loadIcons(QStringLiteral(":/AfterNight/icons/PNG"));
    };
    themeDeps.setActionIcons = [this]() { setActionIcons(); };
    themeDeps.mainDashboard = &impl.dashboard;
    themeDeps.toolbarLockLayout = &impl.lockButton;
    themeDeps.isLayoutLocked = [this]() { return m_impl->core.dockLayout().isLocked(dockTarget()); };
    themeDeps.imageViewControlManager = viewManager;
    themeDeps.dockMainWindow = &impl.dockArea;
    themeDeps.forEachOpenProcess = [this](const std::function<void(TopLevelProcessBaseControl*)>& visit) {
        m_impl->core.openProcesses().forEach(visit);
    };
    themeDeps.mainWindow = this;
    themeDeps.iconProvider = [this](const std::string& iconName) { return m_impl->icon(iconName); };
    impl.themeManager = std::make_unique<an::ui::ThemeManager>(themeDeps);

    an::ui::FileManager::Dependencies fileDeps;
    fileDeps.signalContext = this;
    fileDeps.recentFiles = &core.recentFiles().entriesForEdit();
    fileDeps.lastOpenDirectory = &core.lastOpenDirectoryForEdit();
    fileDeps.recentFilesLimit = [this]() { return m_impl->core.recentFiles().limit(); };
    fileDeps.saveRecentFiles = [this]() { m_impl->core.recentFiles().save(); };
    fileDeps.reloadRecentFilesThumbnails = [this]() {
        if (m_impl->dashboard != nullptr)
        {
            m_impl->dashboard->reloadRecentFilesThumbnails();
        }
    };
    fileDeps.notifyRecentFilesListDeleted = [this]() {
        if (m_impl->dashboard != nullptr)
        {
            m_impl->dashboard->notifyRecentFilesListDeleted();
        }
    };
    fileDeps.rawOpenPolicy = [this]() { return m_impl->core.rawOpenPolicy(); };
    fileDeps.persistLastOpenDirectory = [this](const QString& path) { m_impl->core.setLastOpenDirectory(path); };
    fileDeps.switchToDockManager = [this]() { showViews(); };
    fileDeps.thumbnailsDirPath = [this]() { return m_impl->core.runtimeStorage().getThumbnailsDirPath(); };
    // A file the person opens is shown in a view of theirs.
    fileDeps.viewFactory = &core.viewService().userViews();
    impl.fileManager = std::make_unique<an::ui::FileManager>(fileDeps);

    an::ui::ImageOperationsCoordinator::Dependencies imageDeps;
    imageDeps.activeViewControl = [viewManager]() -> ImageViewControl* {
        return viewManager->getActiveImageViewControl();
    };
    imageDeps.listViewControls = [viewManager]() -> std::vector<ImageViewControl*> {
        return viewManager->getListOfImageViewControls();
    };
    imageDeps.messageParent = [this]() -> QWidget* { return this; };
    imageDeps.notifyErrorMessage = [this](const QString& title, const QString& text) {
        m_impl->core.dialogService().notifyErrorMessage(this, title, text);
    };
    // The image menu's actions that open no window: the processes library's table.
    imageDeps.nonInteractive = an::ui::nonInteractiveImageOperations();
    imageDeps.exportSequence = [](ImageViewControl* control) {
        if (control != nullptr)
        {
            (void)ExportSequenceControl::openFor(*control);
        }
    };
    impl.imageOperations = std::make_unique<an::ui::ImageOperationsCoordinator>(imageDeps);

    an::ui::MaskOverlayCoordinator::Dependencies maskDeps;
    maskDeps.actions = core.actions().lookup();
    maskDeps.imageViewControlManager = viewManager;
    maskDeps.notifyErrorMessage = [this](const QString& title, const QString& text) {
        m_impl->core.dialogService().notifyErrorMessage(this, title, text);
    };
    impl.maskOverlay = std::make_unique<an::ui::MaskOverlayCoordinator>(maskDeps);

    an::ui::DialogOrchestrationCoordinator::Dependencies dialogDeps;
    dialogDeps.mainWindow = this;
    dialogDeps.darkMode = &impl.darkMode;
    dialogDeps.extensionBackgroundWorkCancellationToken = core.extensions().cancellationToken();
    dialogDeps.globalSettingsPersistence = [this]() { return m_impl->core.globalSettingsStore(); };
    dialogDeps.recentFiles = &core.recentFiles().entriesForEdit();
    dialogDeps.mainDashboard = &impl.dashboard;
    dialogDeps.getRecentFilesLimit = [this]() { return m_impl->core.recentFiles().limit(); };
    dialogDeps.iconProvider = [this](const std::string& iconName) { return m_impl->icon(iconName); };
    dialogDeps.removeRecentFileThumbnail = [this](const std::string& fileName) {
        if (m_impl->fileManager)
        {
            m_impl->fileManager->removeRecentFileThumbnail(fileName);
        }
    };
    dialogDeps.persistRecentFiles = [this]() { m_impl->core.recentFiles().save(); };
    dialogDeps.settingsApplied = [this]() { m_impl->core.extensions().settingsApplied(); };
    dialogDeps.updateRecentFilesMenuEntries = [this]() {
        if (m_impl->fileManager)
        {
            m_impl->fileManager->updateRecentFilesMenuEntries();
        }
    };
    impl.dialogs = std::make_unique<an::ui::DialogOrchestrationCoordinator>(dialogDeps);

    an::ui::AppLifecycleCoordinator::Dependencies lifecycleDeps;
    lifecycleDeps.mainWindow = this;
    lifecycleDeps.globalSettingsPersistence = [this]() { return m_impl->core.globalSettingsStore(); };
    lifecycleDeps.imageViewControlManager = viewManager;
    lifecycleDeps.isTerminating = &impl.terminating;
    lifecycleDeps.maxThreadsForBatchProcess = &impl.batchThreads;
    lifecycleDeps.maxThreadsForPreviewProcess = &impl.previewThreads;
    lifecycleDeps.initializePersistence = [this]() { readyFiles(); };
    lifecycleDeps.cleanTemporaryFiles = [this]() { m_impl->core.runtimeStorage().cleanTemporaryFiles(); };
    lifecycleDeps.applyTheme = [this](const QString& themeId) { applyThemeById(themeId); };
    lifecycleDeps.loadIcons = [this]() {
        if (m_impl->themeManager)
        {
            m_impl->themeManager->loadIcons();
        }
    };
    lifecycleDeps.setActionIcons = [this]() { setActionIcons(); };
    lifecycleDeps.refreshProxyStyleTokens = []() {
        if (auto* proxyStyle = an::ui::installedAnProxyStyle())
        {
            proxyStyle->refreshThemeTokens();
        }
    };
    lifecycleDeps.setEnableImageActions = [this](const bool enable) {
        m_impl->core.actionState().setEnableImageActions(enable);
    };
    lifecycleDeps.updateRecentFilesMenuEntries = [this]() {
        if (m_impl->fileManager)
        {
            m_impl->fileManager->updateRecentFilesMenuEntries();
        }
    };
    lifecycleDeps.cancelBackgroundWork = [this]() { m_impl->core.extensions().cancelBackgroundWork(); };
    lifecycleDeps.notifyApplicationClose = [this]() { m_impl->core.eventBus().notifyApplicationClose(); };
    lifecycleDeps.getNumOfOpenProcesses = [this]() {
        return m_impl->core.openProcesses().size() + m_impl->core.extensions().activeSessionCount();
    };
    lifecycleDeps.closeOpenProcesses = [this]() { m_impl->core.extensions().stopSessions(); };
    lifecycleDeps.messageBoxYesNo = [this](QWidget* parent, const QString& title, const QString& text, const bool warn,
                                           int* ret) {
        m_impl->core.dialogService().messageBoxYesNo(parent, title, text, warn, ret);
    };
    lifecycleDeps.showWindowWhereItWas = [this](QWidget& window) {
        an::ui::shell::WindowGeometryStore(m_impl->core).show(window);
    };
    lifecycleDeps.keepWindowPlace = [this](const QWidget& window) {
        an::ui::shell::WindowGeometryStore(m_impl->core).save(window);
    };
    impl.lifecycle = std::make_unique<an::ui::AppLifecycleCoordinator>(lifecycleDeps);
}

void ObservatoryWindow::bindServices()
{
    Impl& impl = *m_impl;
    an::ui::shell::ApplicationCore& core = impl.core;

    // What the actions act on that is this window's: its file workflows, image operations and mask overlay, its
    // dialogs and its dock area.
    an::ui::shell::ActionBindings::Targets targets;
    targets.openFile = [this]() { openFile(); };
    targets.saveFile = [this]() {
        if (m_impl->fileManager)
        {
            m_impl->fileManager->saveFile();
        }
    };
    targets.newSequence = [this]() { newSequence(); };
    targets.exit = [this]() { close(); };
    targets.imageOperations = [this]() -> an::ui::ImageOperationsCoordinator* { return m_impl->imageOperations.get(); };
    targets.maskOverlay = [this]() -> an::ui::MaskOverlayCoordinator* { return m_impl->maskOverlay.get(); };
    targets.launch = [this](const std::string& descriptorId) {
        return m_impl->core.processLaunch().launch(descriptorId);
    };
    targets.sessionConsole = [this](const bool visible) { showConsole(visible); };
    targets.userGuide = [this]() {
        if (m_impl->dialogs)
        {
            m_impl->dialogs->openHelp();
        }
    };
    targets.about = [this]() {
        if (m_impl->dialogs)
        {
            m_impl->dialogs->about();
        }
    };
    targets.systemInformation = [this]() {
        if (m_impl->dialogs)
        {
            m_impl->dialogs->systemInformation();
        }
    };
    targets.settings = [this]() { openSettings(); };
    targets.extensionManager = [this]() { openSettingsAt(an::ui::shell::ExtensionService::managerPageTitle()); };
    targets.lockLayout = [this]() { m_impl->core.dockLayout().lock(dockTarget()); };
    targets.unlockLayout = [this]() { m_impl->core.dockLayout().unlock(dockTarget()); };
    targets.groupAll = [this]() { m_impl->core.dockLayout().groupAll(dockTarget()); };
    targets.splitAll = [this]() { m_impl->core.dockLayout().splitAllEqually(dockTarget()); };
    core.actionBindings().setTargets(std::move(targets));

    an::ui::shell::ActionStateCoordinator::Hooks actionStateHooks;
    actionStateHooks.isTerminating = [this]() { return m_impl->terminating; };
    core.actionState().setHooks(std::move(actionStateHooks));

    an::ui::shell::ViewService::Hooks viewHooks;
    viewHooks.isTerminating = [this]() { return m_impl->terminating; };
    core.viewService().setHooks(std::move(viewHooks));

    an::ui::shell::DialogService::Hooks dialogHooks;
    dialogHooks.openSettingsAt = [this](const QString& pageName) { openSettingsAt(pageName); };
    dialogHooks.openExtensionsAt = [this](const QString& pageName) { openSettingsAt(pageName); };
    core.dialogService().setHooks(std::move(dialogHooks));

    // Files dropped on the window are opened by its file manager.
    core.fileDrops().setHooks({.openFiles = [this](const QStringList& files) { openFiles(files); }});

    an::ui::shell::ExtensionService::Hooks extensionHooks;
    extensionHooks.askYesNo = [this](const QString& title, const QString& text, const bool warn,
                                     std::function<void(int)> answered) {
        if (m_impl->dialogs)
        {
            m_impl->dialogs->openMessageBoxYesNo(this, title, text, warn, std::move(answered));
        }
    };
    extensionHooks.openExtensionSettings = [this](const std::string_view extensionId, QString* diagnostic) {
        return m_impl->dialogs && m_impl->dialogs->openExtensionSettingsDialog(extensionId, diagnostic);
    };
    core.extensions().setHooks(std::move(extensionHooks));
}

void ObservatoryWindow::initialize()
{
    if (m_impl->lifecycle)
    {
        m_impl->lifecycle->initialize();
    }
    m_impl->core.processMenus().refreshStoredLists();
}

// ---- The regions

TopBar* ObservatoryWindow::topBar() const noexcept
{
    return m_impl->topBar;
}

an::ui::AnIconRail* ObservatoryWindow::rail() const noexcept
{
    return m_impl->rail;
}

QStackedWidget* ObservatoryWindow::centre() const noexcept
{
    return m_impl->centre;
}

QWidget* ObservatoryWindow::rightColumn() const noexcept
{
    return m_impl->rightColumn;
}

StatusLine* ObservatoryWindow::statusLine() const noexcept
{
    return m_impl->statusLine;
}

HomePage* ObservatoryWindow::homePage() const noexcept
{
    return m_impl->homePage;
}

WorkspacePage* ObservatoryWindow::workspacePage() const noexcept
{
    return m_impl->workspacePage;
}

WorkspaceHost& ObservatoryWindow::workspaces() noexcept
{
    return *m_impl->workspaces;
}

bool ObservatoryWindow::isHomeShown() const
{
    return m_impl->centre->currentWidget() == m_impl->homePage;
}

bool ObservatoryWindow::isWorkspaceShown() const
{
    return m_impl->centre->currentWidget() == m_impl->workspacePage;
}

// ---- What the shell asks of its window

void ObservatoryWindow::setSessionConsoleVisible(const bool visible)
{
    if (QAction* const action = m_impl->core.actions().action(an::ui::shell::ids::kLogViewer))
    {
        const QSignalBlocker blocker(action);
        action->setChecked(visible);
    }
    showConsole(visible);
}

bool ObservatoryWindow::isSessionConsoleVisible() const
{
    return m_impl->console != nullptr;
}

void ObservatoryWindow::refreshTheme()
{
    if (m_impl->themeManager)
    {
        m_impl->themeManager->refreshTheme();
    }
    if (m_impl->workspaces)
    {
        m_impl->workspaces->refreshTheme();
    }
    m_impl->topBar->updateGeometry();
    m_impl->statusLine->updateGeometry();
    m_impl->rail->updateGeometry();
    update();
}

void ObservatoryWindow::showWithSavedGeometry()
{
    if (m_impl->lifecycle)
    {
        m_impl->lifecycle->showWithSavedGeometry();
    }
    if (m_impl->dialogs)
    {
        m_impl->dialogs->checkExtensionUpdatesOnStartup();
    }
}

void ObservatoryWindow::keepGeometry()
{
    if (m_impl->lifecycle)
    {
        m_impl->lifecycle->saveWindowGeometry();
    }
}

// ---- Where the views are

void ObservatoryWindow::place(ImageViewControl& view, const an::ui::shell::ViewPlacement& placement)
{
    // A view maximized goes back to the dock area, which the new view joins.
    if (ImageViewControl* const maximized = m_impl->workspacePage->maximizedView();
        maximized != nullptr && maximized != &view && !maximized->isInFloatingWindow())
    {
        maximized->switchMaximized();
    }
    an::ui::dockCreatedView(view, placement.neighbour, m_impl->dockArea);
    showViews();
}

void ObservatoryWindow::activate(ImageViewControl& view)
{
    KDDockWidgets::QtWidgets::DockWidget* const dock = view.getDockWidget();
    KDDockWidgets::Core::DockWidget* const controller = dock != nullptr ? dock->dockWidget() : nullptr;
    if (controller != nullptr && (controller->isInMainWindow() || controller->floatingWindow() != nullptr))
    {
        dock->show();
        dock->setAsCurrentTab();
        view.show();
        view.raise();
    }
}

void ObservatoryWindow::viewAdded(ImageViewControl& /*view*/, const an::ui::shell::ViewOrigin /*origin*/) {}

void ObservatoryWindow::viewRemoved(ImageViewControl& view)
{
    if (m_impl->workspacePage->maximizedView() == &view)
    {
        m_impl->workspacePage->restore(view);
    }
}

void ObservatoryWindow::viewRenamed(ImageViewControl& /*view*/, const std::string& /*previousName*/) {}

void ObservatoryWindow::maximize(ImageViewControl& view)
{
    Impl& impl = *m_impl;
    impl.workspacePage->maximize(view);
    // The layout is locked while a view is maximized: its dock keeps a stand-in that must stay where it is.
    if (!impl.core.dockLayout().isLocked(dockTarget()))
    {
        impl.unlockWhenRestored = true;
        impl.core.dockLayout().lock(dockTarget());
    }
    showViews();
    impl.core.viewService().setActiveImageViewControl(&view);
}

void ObservatoryWindow::restore(ImageViewControl& view)
{
    Impl& impl = *m_impl;
    impl.workspacePage->restore(view);
    if (impl.workspacePage->maximizedView() == nullptr && impl.unlockWhenRestored)
    {
        impl.unlockWhenRestored = false;
        impl.core.dockLayout().unlock(dockTarget());
    }
    impl.core.viewService().setActiveImageViewControl(&view);
}

bool ObservatoryWindow::isMaximizedViewActive() const
{
    return isWorkspaceShown() && m_impl->workspacePage->maximizedView() != nullptr;
}

an::ui::shell::DockTarget ObservatoryWindow::dockTarget(const ImageViewControl* /*view*/)
{
    return m_impl->workspaces->target();
}

void ObservatoryWindow::showHome()
{
    // The Session Console is a dock of the workspace's dock area, which stays on screen while it is open.
    if (m_impl->console == nullptr)
    {
        m_impl->centre->setCurrentWidget(m_impl->homePage);
    }
}

void ObservatoryWindow::showViews()
{
    m_impl->centre->setCurrentWidget(m_impl->workspacePage);
}

// ---- The status line

void ObservatoryWindow::showImageInfo(ImageViewControl* /*view*/) {}

void ObservatoryWindow::showZoom(ImageViewControl* /*view*/) {}

void ObservatoryWindow::showCelestialCoordinates(const double /*raDegrees*/, const double /*decDegrees*/) {}

void ObservatoryWindow::clearCelestialCoordinates() {}

void ObservatoryWindow::clearImageInfo() {}

// ---- The window

void ObservatoryWindow::closeEvent(QCloseEvent* event)
{
    if (m_impl->lifecycle && !m_impl->lifecycle->prepareForClose())
    {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
    // A window a dialog left on its own would keep the event loop alive: the application quits with this window.
    QApplication::quit();
}

// ---- What the window does

void ObservatoryWindow::readyFiles()
{
    Impl& impl = *m_impl;
    if (impl.fileManager)
    {
        impl.fileManager->setPersistenceDependencies(impl.core.thumbnailStore(), impl.core.folderStore(),
                                                     impl.core.globalSettingsStore());
    }
    // A settings database of another format was set aside: the person is told where, once the window is up.
    if (const QString notice = impl.core.settingsStoreNotice(); !notice.isEmpty())
    {
        impl.core.notifyInfoMessageAsync(this, tr("Settings"), notice);
    }
}

void ObservatoryWindow::applyThemeById(const QString& themeId)
{
    if (m_impl->themeManager)
    {
        m_impl->themeManager->applyTheme(themeId);
    }
    m_impl->core.extensions().themeChanged();
    refreshTheme();
}

void ObservatoryWindow::setActionIcons()
{
    const std::shared_ptr<IconManager> icons = m_impl->icons;
    if (!icons)
    {
        return;
    }
    m_impl->core.actionBindings().setActionIcons([&icons](const QString& key) { return icons->getIcon(key); });
}

void ObservatoryWindow::openFile()
{
    if (m_impl->fileManager)
    {
        m_impl->fileManager->openFile();
    }
}

void ObservatoryWindow::openFiles(const QStringList& files)
{
    if (m_impl->fileManager)
    {
        m_impl->fileManager->openDroppedFiles(files);
    }
}

void ObservatoryWindow::newSequence()
{
    auto* control = new NewSequenceControl(this);
    control->show();
}

void ObservatoryWindow::openImageIntegration()
{
    if (!m_impl->core.processLaunch().launch(kImageIntegrationProcess) && m_impl->processLauncher)
    {
        m_impl->processLauncher->executeImageIntegration();
    }
}

void ObservatoryWindow::openSettings()
{
    if (m_impl->dialogs)
    {
        m_impl->dialogs->openSettings();
        m_impl->core.processMenus().refreshStoredLists();
    }
}

void ObservatoryWindow::openSettingsAt(const QString& pageName)
{
    if (!m_impl->dialogs)
    {
        return;
    }
    if (an::ui::shell::ExtensionService::isExtensionsPageTitle(pageName))
    {
        m_impl->dialogs->openExtensionsAt(pageName);
        m_impl->core.extensions().refreshSoon();
        return;
    }
    m_impl->dialogs->openSettingsAt(pageName);
    m_impl->core.processMenus().refreshStoredLists();
}

void ObservatoryWindow::showConsole(const bool visible)
{
    Impl& impl = *m_impl;
    if (visible)
    {
        // The console is a dock of the workspace's dock area: the workspace is shown with it.
        showViews();
        if (impl.console != nullptr)
        {
            impl.console->show();
            impl.console->getDockWidget()->show();
            return;
        }
        impl.console = new LogViewerControl(this);
        impl.consoleGone = connect(impl.console, &QObject::destroyed, this, [this]() {
            m_impl->console = nullptr;
            if (QAction* const action = m_impl->core.actions().action(an::ui::shell::ids::kLogViewer))
            {
                const QSignalBlocker blocker(action);
                action->setChecked(false);
            }
            // With no image open, Home is shown again.
            if (m_impl->core.viewService().getListOfViewControls().empty())
            {
                showHome();
            }
        });
        impl.console->initialize();
        impl.dockArea->addDockWidget(impl.console->getDockWidget(), KDDockWidgets::Location_OnBottom);
        return;
    }
    if (impl.console == nullptr)
    {
        return;
    }
    impl.console->getDockWidget()->forceClose();
    impl.console->close();
}

void ObservatoryWindow::arrangementChanged()
{
    // A view floated out of the dock area or docked back: with none left the place of the views stays, as long as one
    // is open; Home is shown once none is.
    if (m_impl->core.viewService().getListOfViewControls().empty())
    {
        showHome();
    }
}

} // namespace an::ui::observatory
