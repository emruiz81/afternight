// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "ObservatoryWindow.h"

#include "ShellActions.h"
#include "chrome/MenuBarOnAlt.h"
#include "chrome/PaletteField.h"
#include "chrome/StatusLine.h"
#include "chrome/TopBar.h"
#include "pages/FloatingImagesPage.h"
#include "pages/HomePage.h"
#include "pages/WorkspacePage.h"
#include "workspaces/FloatingViewChrome.h"
#include "workspaces/WorkspaceHost.h"
#include "workspaces/WorkspaceToolbar.h"

#include "actions/ActionBindings.h"
#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "actions/ActionStateCoordinator.h"
#include "actions/ProcessListDialogs.h"
#include "actions/ProcessMenuModel.h"
#include "actions/ViewMenuBuilder.h"
#include "commands/CommandIndex.h"
#include "core/ApplicationContext.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationEvents.h"
#include "readout/ReadoutController.h"
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

#include "app/AfterNightAppListener.h"
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
#include "components/AnBreadcrumb.h"
#include "components/AnChip.h"
#include "components/AnCommandPalette.h"
#include "components/AnFloatingToolbar.h"
#include "components/AnIconButton.h"
#include "components/AnIconRail.h"
#include "components/AnReadoutColumn.h"
#include "components/RailItem.h"
#include "controls/ExportSequenceControl.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/KDDockWidgetsHelper.h"
#include "controls/ViewChrome.h"
#include "controls/LogViewerControl.h"
#include "controls/MainDashboard.h"
#include "controls/NewSequenceControl.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "theme/AnProxyStyle.h"
#include "theme/ComponentParts.h"

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/TabBar.h>

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMimeData>
#include <QTimer>
#include <QMenu>
#include <QPointer>
#include <QPainter>
#include <QResizeEvent>
#include <QToolButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <functional>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace an::ui::observatory
{

namespace
{

/// The Image Integration process, which the dashboard's button opens.
constexpr char kImageIntegrationProcess[] = "multi_image_stacking.actionImage_Integration";

/// Hears which view is active and which views go, for the window's chrome.
class ViewFollower final : public AfterNightAppListener
{
public:
    explicit ViewFollower(std::function<void(ImageViewControl*)> activeChanged)
        : AfterNightAppListener(nullptr), m_activeChanged(std::move(activeChanged))
    {
    }

    void onActiveViewControlChanged(ImageViewControl* view) override
    {
        if (m_activeChanged)
        {
            m_activeChanged(view);
        }
    }

private:
    std::function<void(ImageViewControl*)> m_activeChanged;
};

/// The scrim laid over the centre and the column while the command palette is open.
class PaletteScrim final : public QWidget
{
public:
    explicit PaletteScrim(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("observatoryPaletteScrim"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        hide();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), an::ui::tokens().colors.scrimSoft);
    }
};

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
    an::ui::AnReadoutColumn* readoutColumn = nullptr;
    StatusLine* statusLine = nullptr;
    MenuBarOnAlt* menuBar = nullptr;
    HomePage* homePage = nullptr;
    WorkspacePage* workspacePage = nullptr;
    FloatingImagesPage* floatingImages = nullptr;
    WorkspaceToolbar* layoutIcons = nullptr;
    FloatingViewChrome* floatingChrome = nullptr;
    std::unique_ptr<WorkspaceHost> workspaces;
    // The window's chrome follows the active view through the core's listeners.
    std::unique_ptr<ViewFollower> follower;
    // The shell's own actions, their commands in the core's index, the palette and its scrim, the top bar's menus.
    std::unique_ptr<ShellActions> shellActions;
    const an::ui::shell::ICommandProvider* shellCommands = nullptr;
    an::ui::AnCommandPalette* palette = nullptr;
    PaletteScrim* scrim = nullptr;
    QMenu* moreMenu = nullptr;
    QMenu* informationMenu = nullptr;
    // The Close action's shortcut before this shell gave it Ctrl+W.
    QKeySequence closeShortcutBefore;
    bool fillingSelector = false;

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
    // What the column shows of the active view: made over the core, fed into the column.
    std::unique_ptr<an::ui::shell::ReadoutController> readout;
    // What makes the window's process menus again when the core's process menus change.
    std::size_t processMenusSubscription = 0;

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
    // Every action is the window's own, so its shortcut works with the menu bar hidden.
    for (QAction* action : core.actions().all())
    {
        addAction(action);
    }
    // The menu bar, from the menu tree over the actions, hidden until Alt is pressed unless the person keeps it.
    an::ui::IGlobalSettingsProvider* const settings = core.globalSettings();
    const bool alwaysShown =
        settings->select(QString::fromLatin1(SettingsKeys::INTERFACE_ALWAYS_SHOW_MENU_BAR),
                         SettingsDefaults::INTERFACE_ALWAYS_SHOW_MENU_BAR ? QStringLiteral("1")
                                                                          : QStringLiteral("0")) == QStringLiteral("1");
    impl.menuBar = new MenuBarOnAlt(*this, core.actions().lookup(), alwaysShown);
    // The shell's own actions, and Ctrl+W on Close, which only this shell gives it: the catalogue's table and the
    // classic shell keep 1.1's shortcuts.
    impl.shellActions = std::make_unique<ShellActions>(*this);
    if (QAction* const closeAction = core.actions().action(an::ui::shell::ids::kClose))
    {
        impl.closeShortcutBefore = closeAction->shortcut();
        closeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    }

    buildRegions();
    makeUnits();
    bindServices();

    // The installed extensions joined the process menus as the application started; the catalogue's actions are
    // connected to what they do, the process actions bound over the registry.
    core.actionBindings().connectProcessActions();
    core.actionBindings().connectActions();
    bindShellActions();
}

ObservatoryWindow::~ObservatoryWindow()
{
    Impl& impl = *m_impl;
    an::ui::shell::ApplicationCore& core = impl.core;

    qApp->removeEventFilter(this);
    if (impl.shellCommands != nullptr)
    {
        core.commands().removeProvider(impl.shellCommands);
        impl.shellCommands = nullptr;
    }
    if (QAction* const closeAction = core.actions().action(an::ui::shell::ids::kClose))
    {
        closeAction->setShortcut(impl.closeShortcutBefore);
    }
    if (impl.follower)
    {
        core.eventBus().unsubscribe(impl.follower.get());
        impl.follower.reset();
    }

    // The readout lets go of the views and the column first: nothing is measured for a window on its way out.
    if (impl.readout)
    {
        impl.readout->setColumn(nullptr);
        impl.readout.reset();
    }

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
    core.processMenus().setHooks({});
    core.processMenus().unsubscribe(impl.processMenusSubscription);
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
    impl.rail->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    body->addWidget(impl.rail);
    QObject::connect(impl.rail, &an::ui::AnIconRail::itemActivated, this,
                     [this](const QString& id) { railActivated(id); });

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
    impl.rightColumn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto* columnLayout = new QVBoxLayout(impl.rightColumn);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(0);
    impl.readoutColumn = new an::ui::AnReadoutColumn(impl.rightColumn);
    impl.readoutColumn->setObjectName(QStringLiteral("observatoryReadout"));
    columnLayout->addWidget(impl.readoutColumn);
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
    FloatingImagesPage::Dependencies floatingDeps;
    floatingDeps.dockHere = [this](ImageViewControl& view) { dockIntoWorkspace(view); };
    floatingDeps.dockAll = [this]() {
        for (ImageViewControl* view : m_impl->floatingImages->views())
        {
            dockIntoWorkspace(*view);
        }
    };
    floatingDeps.openFile = [this]() { openFile(); };
    impl.floatingImages = new FloatingImagesPage(std::move(floatingDeps));
    impl.workspacePage = new WorkspacePage(*impl.dockArea, *impl.floatingImages, impl.centre);
    impl.centre->addWidget(impl.workspacePage);

    // The view's menu fills the toolbars' More: the menus it shares are the menu bar's.
    const an::ui::shell::ViewChromeBinder::MenuFiller viewMenu = [this](QMenu& menu, ImageViewControl& view) {
        an::ui::shell::ViewMenuBuilder(m_impl->core).buildViewMenu(menu, view, [this](std::string_view menuId) {
            return m_impl->menuBar->menu(menuId);
        });
    };
    impl.workspacePage->binder().setMenuFiller(viewMenu);
    impl.floatingChrome = new FloatingViewChrome(this);
    impl.floatingChrome->setMenuFiller(viewMenu);

    // The layout icons sit at the right of the top bar while a workspace is shown.
    WorkspaceToolbar::Dependencies iconDeps;
    iconDeps.actions = impl.core.actions().lookup();
    iconDeps.isLocked = [this]() { return m_impl->core.dockLayout().isLocked(dockTarget()); };
    iconDeps.setLocked = [this](const bool locked) { lockLayout(locked); };
    iconDeps.icon = [this](const QString& name) { return m_impl->icon(name.toStdString()); };
    impl.layoutIcons = new WorkspaceToolbar(std::move(iconDeps), impl.topBar);
    impl.topBar->trailing()->addWidget(impl.layoutIcons);
    buildTopBarTrailing();

    // The status line's own parts: the view selector, the readout's button, the Console chip.
    QObject::connect(impl.statusLine->viewSelector(), &QComboBox::currentTextChanged, this,
                     [this](const QString& name) {
                         if (!m_impl->fillingSelector && !name.isEmpty())
                         {
                             m_impl->core.viewService().setActiveViewByName(name.toStdString());
                         }
                     });
    QObject::connect(impl.statusLine->readoutButton(), &QAbstractButton::clicked, this, [this]() {
        if (QAction* const readout = m_impl->shellActions->action(ids::kToggleReadout))
        {
            readout->trigger();
        }
    });
    QObject::connect(impl.statusLine->consoleChip(), &QAbstractButton::clicked, this,
                     [this]() { setSessionConsoleVisible(true); });

    // The chrome follows the active view.
    impl.follower = std::make_unique<ViewFollower>([this](ImageViewControl* view) { activeViewChanged(view); });
    impl.core.eventBus().subscribe(impl.follower.get());

    // A tab's menu, and Esc while a view is maximized, are heard before the widgets they reach.
    qApp->installEventFilter(this);
    setAcceptDrops(true);

    impl.centre->setCurrentWidget(impl.homePage);
    connectDashboard();

    // The command palette over the core's index of commands, and its scrim over the centre and the column.
    impl.topBar->setIconProvider([this](const QString& name) { return m_impl->icon(name.toStdString()); });
    impl.palette = new an::ui::AnCommandPalette(this);
    impl.palette->setObjectName(QStringLiteral("observatoryCommandPalette"));
    impl.palette->setSource(&impl.core.commands());
    impl.palette->setIconResolver([this](const QString& key) { return m_impl->icon(key.toStdString()); });
    impl.scrim = new PaletteScrim(central);
    QObject::connect(impl.palette, &an::ui::AnCommandPalette::closed, this, [this]() { m_impl->scrim->hide(); });
    QObject::connect(impl.palette, &an::ui::AnCommandPalette::allMenusRequested, this, [this]() {
        const QWidget* const field = m_impl->topBar->paletteField();
        m_impl->moreMenu->popup(field->mapToGlobal(QPoint(0, field->height())));
    });
    QObject::connect(impl.topBar->paletteField(), &QAbstractButton::clicked, this, [this]() { openPalette(); });

    // The readout follows the active view into the column.
    impl.readout = std::make_unique<an::ui::shell::ReadoutController>(impl.core);
    impl.readout->setColumn(impl.readoutColumn);

    // The rail: the places, and at its foot the Session Console and Settings.
    std::vector<an::ui::RailItem> items;
    items.push_back(an::ui::RailItem{.id = QString::fromLatin1(kRailHome),
                                     .section = an::ui::RailSection::Place,
                                     .iconKey = QStringLiteral("home"),
                                     .text = tr("Home")});
    items.push_back(an::ui::RailItem{.id = QString::fromLatin1(kRailWorkspace),
                                     .section = an::ui::RailSection::Place,
                                     .iconKey = QStringLiteral("images"),
                                     .text = impl.workspaces->workspaceName()});
    items.push_back(an::ui::RailItem{.id = QString::fromLatin1(kRailIntegration),
                                     .section = an::ui::RailSection::Place,
                                     .iconKey = QStringLiteral("layers"),
                                     .text = tr("Integration")});
    items.push_back(an::ui::RailItem{.id = QString::fromLatin1(kRailConsole),
                                     .section = an::ui::RailSection::Foot,
                                     .iconKey = QStringLiteral("terminal"),
                                     .text = tr("Session Console")});
    items.push_back(an::ui::RailItem{.id = QString::fromLatin1(kRailSettings),
                                     .section = an::ui::RailSection::Foot,
                                     .iconKey = QStringLiteral("settings"),
                                     .text = tr("Settings")});
    impl.rail->setItems(items);
    placeChanged();
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
    fileDeps.recentFilesMenu = impl.menuBar->menu("menuOpen_Recent");
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
    // The layout starts locked when the person asked for it.
    lifecycleDeps.lockLayout = [this]() { lockLayout(true); };
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
    targets.lockLayout = [this]() { lockLayout(true); };
    targets.unlockLayout = [this]() { lockLayout(false); };
    targets.groupAll = [this]() { m_impl->core.dockLayout().groupAll(dockTarget()); };
    targets.splitAll = [this]() { m_impl->core.dockLayout().splitAllEqually(dockTarget()); };
    core.actionBindings().setTargets(std::move(targets));

    // The actions' state sets the menu bar's menus, and stops while the window closes.
    an::ui::shell::ActionStateCoordinator::Hooks actionStateHooks;
    actionStateHooks.setMenuEnabled = [this](const std::string_view menuId, const bool enabled) {
        if (QMenu* const menu = m_impl->menuBar->menu(menuId))
        {
            menu->setEnabled(enabled);
        }
    };
    actionStateHooks.isTerminating = [this]() { return m_impl->terminating; };
    core.actionState().setHooks(std::move(actionStateHooks));

    // The process menus' own entries ask the person, and the menus are made again from the core's whenever it changes.
    an::ui::shell::ProcessMenuModel::Hooks processMenuHooks;
    processMenuHooks.clearRecentProcesses = [this]() {
        if (an::ui::shell::clearRecentProcesses(m_impl->core))
        {
            m_impl->core.processMenus().notifyChanged(an::ui::shell::ProcessMenuModel::Change::RecentProcesses);
        }
    };
    processMenuHooks.manageFavoriteProcesses = [this]() {
        if (an::ui::shell::manageFavoriteProcesses(m_impl->core, this))
        {
            m_impl->core.processMenus().notifyChanged(an::ui::shell::ProcessMenuModel::Change::FavoriteProcesses);
        }
    };
    processMenuHooks.clearFavoriteProcesses = [this]() {
        if (an::ui::shell::clearFavoriteProcesses(m_impl->core))
        {
            m_impl->core.processMenus().notifyChanged(an::ui::shell::ProcessMenuModel::Change::FavoriteProcesses);
        }
    };
    core.processMenus().setHooks(std::move(processMenuHooks));
    impl.processMenusSubscription =
        core.processMenus().subscribe([this](const an::ui::shell::ProcessMenuModel::Change change) {
            switch (change)
            {
            case an::ui::shell::ProcessMenuModel::Change::RecentProcesses:
                if (QMenu* const menu = m_impl->menuBar->menu("menuRecent_Processes"))
                {
                    m_impl->core.processMenus().buildRecentProcessesMenu(*menu);
                }
                break;
            case an::ui::shell::ProcessMenuModel::Change::FavoriteProcesses:
                if (QMenu* const menu = m_impl->menuBar->menu("menuFavorite_Processes"))
                {
                    m_impl->core.processMenus().buildFavoriteProcessesMenu(*menu);
                }
                break;
            case an::ui::shell::ProcessMenuModel::Change::Extensions:
                updateExtensionMenus();
                break;
            }
        });
    // The extensions the application found as it started join the menus now.
    updateExtensionMenus();

    an::ui::shell::ViewService::Hooks viewHooks;
    viewHooks.refreshViewList = [this]() { refreshViewSelector(); };
    // The view's menu opens where the person clicked.
    viewHooks.contextMenu = [this](ImageViewControl* view, QContextMenuEvent* event) {
        if (view != nullptr && event != nullptr)
        {
            showViewMenu(*view, event->globalPos());
        }
    };
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

an::ui::AnReadoutColumn* ObservatoryWindow::readoutColumn() const noexcept
{
    return m_impl->readoutColumn;
}

MenuBarOnAlt& ObservatoryWindow::menuBarOnAlt() const noexcept
{
    return *m_impl->menuBar;
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

WorkspaceToolbar* ObservatoryWindow::layoutIcons() const noexcept
{
    return m_impl->layoutIcons;
}

FloatingViewChrome& ObservatoryWindow::floatingViewChrome() const noexcept
{
    return *m_impl->floatingChrome;
}

ShellActions& ObservatoryWindow::shellActions() const noexcept
{
    return *m_impl->shellActions;
}

an::ui::AnCommandPalette* ObservatoryWindow::palette() const noexcept
{
    return m_impl->palette;
}

QWidget* ObservatoryWindow::paletteScrim() const noexcept
{
    return m_impl->scrim;
}

QMenu* ObservatoryWindow::moreMenu() const noexcept
{
    return m_impl->moreMenu;
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
    // The view's tools are outside it in this shell: the workspace's floating toolbar and history strip.
    view.setChromeMode(an::ui::ViewChromeMode::External);
    an::ui::dockCreatedView(view, placement.neighbour, m_impl->dockArea);
    // A view floats or docks by a drag, a double click or its strip's button: what the workspace shows follows, once
    // the dock library has moved it.
    if (KDDockWidgets::QtWidgets::DockWidget* const dock = view.getDockWidget())
    {
        QObject::connect(dock, &KDDockWidgets::QtWidgets::DockWidget::isFloatingChanged, this,
                         [this](bool) { QTimer::singleShot(0, this, [this]() { updateWorkspacePage(); }); });
    }
    showViews();
    updateWorkspacePage();
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

void ObservatoryWindow::viewAdded(ImageViewControl& /*view*/, const an::ui::shell::ViewOrigin /*origin*/)
{
    refreshViewSelector();
    placeChanged();
}

void ObservatoryWindow::viewRemoved(ImageViewControl& view)
{
    if (m_impl->workspacePage->maximizedView() == &view)
    {
        m_impl->workspacePage->restore(view);
    }
    if (m_impl->workspacePage->binder().toolbarView() == &view ||
        m_impl->workspacePage->binder().historyView() == &view)
    {
        m_impl->workspacePage->bindView(nullptr);
    }
    refreshViewSelector();
    updateWorkspacePage();
    placeChanged();
}

void ObservatoryWindow::viewRenamed(ImageViewControl& /*view*/, const std::string& /*previousName*/)
{
    refreshViewSelector();
    updateBreadcrumb();
}

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
    offerTheLock(true);
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
    offerTheLock(impl.workspacePage->maximizedView() != nullptr);
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
    placeChanged();
}

void ObservatoryWindow::showViews()
{
    m_impl->centre->setCurrentWidget(m_impl->workspacePage);
    placeChanged();
}

// ---- The status line

void ObservatoryWindow::showImageInfo(ImageViewControl* view)
{
    m_impl->statusLine->showImageInfo(view);
}

void ObservatoryWindow::showZoom(ImageViewControl* view)
{
    m_impl->statusLine->showZoom(view);
}

void ObservatoryWindow::showCelestialCoordinates(const double raDegrees, const double decDegrees)
{
    m_impl->statusLine->showCelestialCoordinates(raDegrees, decDegrees);
}

void ObservatoryWindow::clearCelestialCoordinates()
{
    m_impl->statusLine->clearCelestialCoordinates();
}

void ObservatoryWindow::clearImageInfo()
{
    m_impl->statusLine->clearImageInfo();
}

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

void ObservatoryWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event != nullptr && event->mimeData() != nullptr && an::ui::shell::FileDropHandler::takes(*event->mimeData()))
    {
        event->acceptProposedAction();
    }
}

void ObservatoryWindow::dropEvent(QDropEvent* event)
{
    if (event != nullptr && event->mimeData() != nullptr && m_impl->core.fileDrops().drop(*event->mimeData()))
    {
        event->acceptProposedAction();
    }
}

bool ObservatoryWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event == nullptr)
    {
        return QMainWindow::eventFilter(watched, event);
    }
    // A tab's menu: the menu of the tab's view.
    if (event->type() == QEvent::ContextMenu)
    {
        if (auto* const tabBar = dynamic_cast<AN_TabBar*>(watched))
        {
            const auto* menuEvent = static_cast<QContextMenuEvent*>(event);
            if (showTabMenu(*tabBar, menuEvent->pos(), menuEvent->globalPos()))
            {
                return true;
            }
        }
    }
    // Esc restores the maximized view, as F11 does, when nothing else on the window takes it.
    if (event->type() == QEvent::KeyPress && m_impl->workspacePage->maximizedView() != nullptr)
    {
        const auto* key = static_cast<QKeyEvent*>(event);
        const auto* widget = qobject_cast<QWidget*>(watched);
        if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier && widget != nullptr &&
            widget->window() == this && QApplication::activePopupWidget() == nullptr &&
            QApplication::activeModalWidget() == nullptr)
        {
            m_impl->workspacePage->maximizedView()->switchMaximized();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void ObservatoryWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    // The column narrows in a narrow window and folds a card in a short one; the palette's field takes its form.
    if (m_impl->readoutColumn != nullptr)
    {
        m_impl->readoutColumn->setWindowSize(size());
    }
    if (m_impl->topBar != nullptr)
    {
        m_impl->topBar->setWindowWidth(width());
    }
    placePaletteScrim();
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
    refreshChromeIcons();
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

void ObservatoryWindow::placeChanged()
{
    Impl& impl = *m_impl;
    // The readout is beside the workspace, and beside Home while an image is open.
    const bool viewsOpen = !impl.core.viewService().getListOfViewControls().empty();
    const bool column = isWorkspaceShown() || (isHomeShown() && viewsOpen);
    if (impl.rightColumn->isVisibleTo(this) != column)
    {
        impl.rightColumn->setVisible(column);
    }
    // The layout icons are the workspace's.
    if (impl.layoutIcons != nullptr)
    {
        impl.layoutIcons->setVisible(isWorkspaceShown());
    }
    updateRail();
    updateBreadcrumb();
}

void ObservatoryWindow::activeViewChanged(ImageViewControl* view)
{
    Impl& impl = *m_impl;
    const std::vector<ImageViewControl*> views = impl.core.viewService().getListOfViewControls();
    // The workspace's chrome shows the active view of the workspace: a view that floats has its own.
    if (view == nullptr || !view->isInFloatingWindow())
    {
        impl.workspacePage->bindView(view);
    }
    int docked = 0;
    for (ImageViewControl* each : views)
    {
        if (each != nullptr && !each->isInFloatingWindow())
        {
            ++docked;
        }
    }
    impl.workspacePage->binder().setViewCount(docked);
    refreshViewSelector();
    updateBreadcrumb();
}

void ObservatoryWindow::refreshViewSelector()
{
    Impl& impl = *m_impl;
    QComboBox* const selector = impl.statusLine->viewSelector();
    const std::vector<ImageViewControl*> views = impl.core.viewService().getListOfViewControls();
    ImageViewControl* const active = impl.core.viewService().getActiveImageViewControl();
    impl.fillingSelector = true;
    selector->clear();
    for (ImageViewControl* view : views)
    {
        if (view != nullptr)
        {
            selector->addItem(QString::fromUtf8(view->getUniqueName()));
        }
    }
    if (active != nullptr)
    {
        selector->setCurrentText(QString::fromUtf8(active->getUniqueName()));
    }
    selector->setVisible(selector->count() > 0);
    impl.fillingSelector = false;
}

void ObservatoryWindow::showViewMenu(ImageViewControl& view, const QPoint& globalPos)
{
    QMenu menu(&view);
    an::ui::shell::ViewMenuBuilder(m_impl->core).buildViewMenu(menu, view, [this](std::string_view menuId) {
        return m_impl->menuBar->menu(menuId);
    });
    menu.exec(globalPos);
}

bool ObservatoryWindow::showTabMenu(QWidget& tabBar, const QPoint& position, const QPoint& globalPos)
{
    auto* const strip = dynamic_cast<KDDockWidgets::QtWidgets::TabBar*>(&tabBar);
    if (strip == nullptr || strip->tabBar() == nullptr)
    {
        return false;
    }
    KDDockWidgets::Core::DockWidget* const tab = strip->tabBar()->dockWidgetAt(position);
    if (tab == nullptr)
    {
        return false;
    }
    for (ImageViewControl* view : m_impl->core.viewService().getListOfViewControls())
    {
        if (view != nullptr && view->getDockWidget() != nullptr && view->getDockWidget()->dockWidget() == tab)
        {
            QMenu menu(&tabBar);
            an::ui::shell::ViewMenuBuilder(m_impl->core).buildTabMenu(menu, *view);
            menu.exec(globalPos);
            return true;
        }
    }
    return false;
}

void ObservatoryWindow::dockIntoWorkspace(ImageViewControl& view)
{
    KDDockWidgets::QtWidgets::DockWidget* const dock = view.getDockWidget();
    if (dock == nullptr || m_impl->dockArea == nullptr)
    {
        return;
    }
    m_impl->dockArea->addDockWidget(dock, KDDockWidgets::Location_OnRight);
    // Docked again, its tools are the workspace's.
    QTimer::singleShot(0, this, [this]() {
        updateWorkspacePage();
        showViews();
    });
}

void ObservatoryWindow::updateWorkspacePage()
{
    Impl& impl = *m_impl;
    std::vector<ImageViewControl*> views;
    std::vector<ImageViewControl*> floating;
    int docked = 0;
    for (ImageViewControl* view : impl.core.viewService().getListOfViewControls())
    {
        if (view == nullptr)
        {
            continue;
        }
        views.push_back(view);
        if (view->isInFloatingWindow())
        {
            floating.push_back(view);
        }
        else
        {
            ++docked;
        }
    }
    impl.floatingChrome->update(views);
    // The only workspace with every image floating lists them; with a docked one, the dock area is shown.
    const bool listFloating = docked == 0 && !floating.empty();
    if (listFloating)
    {
        impl.floatingImages->setViews(floating);
    }
    impl.workspacePage->showFloatingImages(listFloating);
    impl.workspacePage->binder().setViewCount(docked);
    ImageViewControl* const bound = impl.workspacePage->binder().toolbarView();
    if (bound != nullptr && bound->isInFloatingWindow())
    {
        impl.workspacePage->bindView(nullptr);
    }
}

void ObservatoryWindow::offerTheLock(const bool maximized)
{
    Impl& impl = *m_impl;
    QAction* const lock = impl.core.actions().action(an::ui::shell::ids::kLockLayout);
    QAction* const unlock = impl.core.actions().action(an::ui::shell::ids::kUnlockLayout);
    QAction* const group = impl.core.actions().action(an::ui::shell::ids::kGroupAllImages);
    QAction* const split = impl.core.actions().action(an::ui::shell::ids::kSplitAllImages);
    const bool locked = impl.core.dockLayout().isLocked(dockTarget());
    // While a view is maximized the layout is not the person's to change; restored, the lock offers what the layout
    // is not, and the icons say what a click does.
    const auto apply = [lock, unlock, group, split, locked, maximized]() {
        if (lock != nullptr)
        {
            lock->setEnabled(!maximized && !locked);
        }
        if (unlock != nullptr)
        {
            unlock->setEnabled(!maximized && locked);
        }
        if (group != nullptr)
        {
            group->setEnabled(!maximized);
        }
        if (split != nullptr)
        {
            split->setEnabled(!maximized);
        }
    };
    apply();
    // The actions' state is set again as the active view changes: what is offered is put back after it.
    QTimer::singleShot(0, this, apply);
    if (impl.layoutIcons != nullptr)
    {
        impl.layoutIcons->lockButton()->setEnabled(!maximized);
        impl.layoutIcons->refreshLock();
    }
}

void ObservatoryWindow::buildTopBarTrailing()
{
    Impl& impl = *m_impl;
    const auto button = [&impl](const char* name, const QString& iconFile, const QString& tip) {
        auto* made = new QToolButton(impl.topBar);
        made->setObjectName(QString::fromLatin1(name));
        made->setAutoRaise(true);
        made->setFixedSize(an::ui::parts::kTopBarButton, an::ui::parts::kTopBarButton);
        made->setIconSize(QSize(an::ui::parts::kTopBarIcon, an::ui::parts::kTopBarIcon));
        // The icon is the theme's, set again whenever the theme changes.
        made->setProperty("iconName", iconFile);
        made->setIcon(impl.icon(iconFile.toStdString()));
        made->setToolTip(tip);
        impl.topBar->trailing()->addWidget(made);
        return made;
    };
    const auto withKeys = [](const QAction* action) {
        if (action == nullptr)
        {
            return QString();
        }
        const QString text = QString(action->text()).remove(QLatin1Char('&'));
        return action->shortcut().isEmpty()
                   ? text
                   : QStringLiteral("%1 (%2)").arg(text, action->shortcut().toString(QKeySequence::NativeText));
    };

    // Night, a toggle: the shell's action.
    QAction* const night = impl.shellActions->action(ids::kToggleNight);
    QToolButton* const nightButton = button("topBarNightButton", QStringLiteral("moon"), withKeys(night));
    night->setIcon(nightButton->icon());
    nightButton->setDefaultAction(night);
    nightButton->setToolTip(withKeys(night));

    // Help: the user guide.
    QAction* const guide = impl.core.actions().action(an::ui::shell::ids::kUserGuide);
    QToolButton* const helpButton = button("topBarHelpButton", QStringLiteral("help"), withKeys(guide));
    QObject::connect(helpButton, &QToolButton::clicked, this, [guide]() {
        if (guide != nullptr)
        {
            guide->trigger();
        }
    });

    // Information: About, About Qt and System Information.
    QToolButton* const information = button("topBarInformationButton", QStringLiteral("info"), tr("Information"));
    impl.informationMenu = new QMenu(information);
    impl.informationMenu->setObjectName(QStringLiteral("topBarInformationMenu"));
    for (const std::string_view id :
         {an::ui::shell::ids::kAboutAfterNight, an::ui::shell::ids::kAboutQt, an::ui::shell::ids::kSystemInformation})
    {
        if (QAction* const action = impl.core.actions().action(id))
        {
            impl.informationMenu->addAction(action);
        }
    }
    information->setMenu(impl.informationMenu);
    information->setPopupMode(QToolButton::InstantPopup);

    // More: the menu tree, as the menu bar holds it.
    QToolButton* const more = button("topBarMoreButton", QStringLiteral("more_horiz"), tr("More"));
    impl.moreMenu = new QMenu(more);
    impl.moreMenu->setObjectName(QStringLiteral("topBarMoreMenu"));
    for (QAction* top : impl.menuBar->bar()->actions())
    {
        if (top->menu() != nullptr)
        {
            impl.moreMenu->addMenu(top->menu());
        }
    }
    more->setMenu(impl.moreMenu);
    more->setPopupMode(QToolButton::InstantPopup);
}

void ObservatoryWindow::refreshChromeIcons()
{
    Impl& impl = *m_impl;
    if (impl.topBar == nullptr)
    {
        return;
    }
    for (QToolButton* button : impl.topBar->findChildren<QToolButton*>())
    {
        const QString name = button->property("iconName").toString();
        if (!name.isEmpty())
        {
            const QIcon themed = impl.icon(name.toStdString());
            if (button->defaultAction() != nullptr)
            {
                button->defaultAction()->setIcon(themed);
            }
            button->setIcon(themed);
        }
    }
    impl.topBar->refreshIcons();
    if (impl.layoutIcons != nullptr)
    {
        impl.layoutIcons->refreshLock();
    }
}

void ObservatoryWindow::bindShellActions()
{
    Impl& impl = *m_impl;
    ShellActions& actions = *impl.shellActions;
    QObject::connect(actions.action(ids::kShowHome), &QAction::triggered, this, [this]() { showHome(); });
    QObject::connect(actions.action(ids::kCommandPalette), &QAction::triggered, this, [this]() { openPalette(); });

    // The readout's collapse, both ways.
    QAction* const readout = actions.action(ids::kToggleReadout);
    readout->setChecked(!impl.readoutColumn->isCollapsed());
    QObject::connect(readout, &QAction::toggled, this,
                     [this](const bool shown) { m_impl->readoutColumn->setCollapsed(!shown); });
    QObject::connect(impl.readoutColumn, &an::ui::AnReadoutColumn::collapsedChanged, this,
                     [this](const bool collapsed) {
                         QAction* const toggle = m_impl->shellActions->action(ids::kToggleReadout);
                         const QSignalBlocker blocker(toggle);
                         toggle->setChecked(!collapsed);
                     });

    QAction* const night = actions.action(ids::kToggleNight);
    night->setChecked(an::ui::nightModeSet());
    QObject::connect(night, &QAction::toggled, this, [this](const bool on) { setNight(on); });

    QAction* const compact = actions.action(ids::kDensityCompact);
    QAction* const comfortable = actions.action(ids::kDensityComfortable);
    (an::ui::themeDensity() == an::ui::ThemeDensity::Comfortable ? comfortable : compact)->setChecked(true);
    QObject::connect(compact, &QAction::triggered, this, [this]() { setDensity(false); });
    QObject::connect(comfortable, &QAction::triggered, this, [this]() { setDensity(true); });

    // The View menu of the menu bar lists them after what 1.1 had there.
    if (QMenu* const view = impl.menuBar->menu("menuView_2"))
    {
        view->addSeparator();
        view->addAction(actions.action(ids::kShowHome));
        view->addAction(actions.action(ids::kCommandPalette));
        view->addAction(readout);
        view->addAction(night);
        QMenu* const density = view->addMenu(tr("Density"));
        density->setObjectName(QStringLiteral("menuDensity"));
        density->addAction(compact);
        density->addAction(comfortable);
    }

    // The palette offers them with the window's other views.
    std::unique_ptr<an::ui::shell::ICommandProvider> provider = actions.commandProvider();
    impl.shellCommands = provider.get();
    impl.core.commands().addProvider(std::move(provider));
    applyCaptionColour();
}

void ObservatoryWindow::openPalette()
{
    Impl& impl = *m_impl;
    if (impl.palette == nullptr || impl.palette->isVisible())
    {
        return;
    }
    // Centred over the centre and the column, 12 px under the top bar.
    const QRect centreAndColumn = impl.centre->geometry().united(
        impl.rightColumn->isVisible() ? impl.rightColumn->geometry() : impl.centre->geometry());
    const QPoint anchor =
        centralWidget()->mapToGlobal(QPoint(centreAndColumn.center().x(), impl.topBar->geometry().bottom() + 1));
    placePaletteScrim();
    impl.scrim->show();
    impl.scrim->raise();
    impl.palette->open(anchor, height());
}

void ObservatoryWindow::placePaletteScrim()
{
    Impl& impl = *m_impl;
    if (impl.scrim == nullptr || impl.centre == nullptr)
    {
        return;
    }
    QRect area = impl.centre->geometry();
    if (impl.rightColumn->isVisible())
    {
        area = area.united(impl.rightColumn->geometry());
    }
    // The centre's geometry is in its row's coordinates: the scrim is laid in the central widget's.
    const QPoint origin = impl.centre->parentWidget()->mapTo(centralWidget(), area.topLeft());
    impl.scrim->setGeometry(QRect(origin, area.size()));
}

void ObservatoryWindow::updateBreadcrumb()
{
    Impl& impl = *m_impl;
    if (impl.topBar == nullptr)
    {
        return;
    }
    QStringList parts;
    if (isWorkspaceShown())
    {
        parts.append(impl.workspaces->workspaceName());
        if (ImageViewControl* const active = impl.core.viewService().getActiveImageViewControl())
        {
            parts.append(QString::fromUtf8(active->getUniqueName()));
        }
    }
    else
    {
        parts.append(tr("Home"));
    }
    impl.topBar->breadcrumb()->setParts(parts);
}

void ObservatoryWindow::setNight(const bool night)
{
    if (an::ui::nightModeSet() == night)
    {
        return;
    }
    an::ui::setNightMode(night);
    an::ui::IGlobalSettingsProvider* const settings = m_impl->core.globalSettings();
    applyThemeById(settings->select(QString::fromLatin1(SettingsKeys::INTERFACE_THEME),
                                    QString::fromLatin1(SettingsDefaults::INTERFACE_THEME)));
    applyCaptionColour();
}

void ObservatoryWindow::setDensity(const bool comfortable)
{
    const an::ui::ThemeDensity density =
        comfortable ? an::ui::ThemeDensity::Comfortable : an::ui::ThemeDensity::Compact;
    an::ui::IGlobalSettingsProvider* const settings = m_impl->core.globalSettings();
    (void)settings->upsert(QString::fromLatin1(SettingsKeys::INTERFACE_DENSITY),
                           comfortable ? QStringLiteral("comfortable") : QStringLiteral("compact"));
    if (an::ui::themeDensity() == density)
    {
        return;
    }
    an::ui::setThemeDensity(density);
    applyThemeById(settings->select(QString::fromLatin1(SettingsKeys::INTERFACE_THEME),
                                    QString::fromLatin1(SettingsDefaults::INTERFACE_THEME)));
}

void ObservatoryWindow::applyCaptionColour()
{
#ifdef _WIN32
    // Under Night the native title bar is drawn in the darkest surface with the primary text, where the system has the
    // attribute (Windows 11): a white caption would undo the dark adaptation Night is for. Off, the system's own again.
    constexpr DWORD kCaptionColour = 35;
    constexpr DWORD kTextColour = 36;
    constexpr DWORD kDefaultColour = 0xFFFFFFFF;
    using SetAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static const SetAttribute setAttribute = []() -> SetAttribute {
        HMODULE const library = LoadLibraryW(L"dwmapi.dll");
        return library != nullptr ? reinterpret_cast<SetAttribute>(
                                        reinterpret_cast<void*>(GetProcAddress(library, "DwmSetWindowAttribute")))
                                  : nullptr;
    }();
    if (setAttribute == nullptr || !testAttribute(Qt::WA_WState_Created))
    {
        return;
    }
    const auto colourOf = [](const QColor& colour) {
        return static_cast<DWORD>(RGB(colour.red(), colour.green(), colour.blue()));
    };
    const bool night = an::ui::nightModeSet() && an::ui::styleProfile() == an::ui::StyleProfile::Observatory;
    const an::ui::DesignTokens& t = an::ui::tokens();
    const DWORD caption = night ? colourOf(t.layers.surface0) : kDefaultColour;
    const DWORD text = night ? colourOf(t.colors.textPrimary) : kDefaultColour;
    const HWND handle = reinterpret_cast<HWND>(winId());
    (void)setAttribute(handle, kCaptionColour, &caption, sizeof(caption));
    (void)setAttribute(handle, kTextColour, &text, sizeof(text));
#endif
}

void ObservatoryWindow::lockLayout(const bool locked)
{
    if (locked)
    {
        m_impl->core.dockLayout().lock(dockTarget());
    }
    else
    {
        m_impl->core.dockLayout().unlock(dockTarget());
    }
    if (m_impl->layoutIcons != nullptr)
    {
        m_impl->layoutIcons->refreshLock();
    }
}

void ObservatoryWindow::updateRail()
{
    Impl& impl = *m_impl;
    if (impl.rail == nullptr)
    {
        return;
    }
    const QString selected = isWorkspaceShown() ? QString::fromLatin1(kRailWorkspace) : QString::fromLatin1(kRailHome);
    for (an::ui::RailItem item : impl.rail->items())
    {
        const bool place = item.section == an::ui::RailSection::Place;
        const bool shouldBeSelected = place && item.id == selected;
        if (item.selected != shouldBeSelected)
        {
            item.selected = shouldBeSelected;
            impl.rail->updateItem(item);
        }
    }
}

void ObservatoryWindow::railActivated(const QString& id)
{
    Impl& impl = *m_impl;
    if (id == QLatin1String(kRailHome))
    {
        showHome();
    }
    else if (id == QLatin1String(kRailWorkspace))
    {
        // No image is open: Home is the place for it.
        if (!impl.core.viewService().getListOfViewControls().empty() || impl.console != nullptr)
        {
            showViews();
        }
    }
    else if (id == QLatin1String(kRailIntegration))
    {
        openImageIntegration();
    }
    else if (id == QLatin1String(kRailConsole))
    {
        setSessionConsoleVisible(impl.console == nullptr);
    }
    else if (id == QLatin1String(kRailSettings))
    {
        openSettings();
    }
}

void ObservatoryWindow::updateExtensionMenus()
{
    const an::ui::shell::ProcessMenuModel& menus = m_impl->core.processMenus();
    for (const an::ui::shell::ProcessMenuModel::Category& category : an::ui::shell::ProcessMenuModel::categories())
    {
        if (QMenu* const menu = m_impl->menuBar->menu(category.menuId))
        {
            menus.appendExtensionProcesses(*menu, category.menuId);
        }
    }
    if (QMenu* const extensions = m_impl->menuBar->menu("menuExtensions"))
    {
        menus.buildExtensionFamilyMenus(*extensions);
    }
}

void ObservatoryWindow::arrangementChanged()
{
    updateWorkspacePage();
    // A view floated out of the dock area or docked back: with none left the place of the views stays, as long as one
    // is open; Home is shown once none is.
    if (m_impl->core.viewService().getListOfViewControls().empty())
    {
        showHome();
    }
}

} // namespace an::ui::observatory
