// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "ObservatoryWindow.h"

#include "ShellActions.h"
#include "chrome/ConsoleDrawer.h"
#include "chrome/MenuBarOnAlt.h"
#include "chrome/PaletteField.h"
#include "chrome/StatusLine.h"
#include "chrome/TopBar.h"
#include "pages/FloatingImagesPage.h"
#include "pages/HomePage.h"
#include "pages/TaskPage.h"
#include "pages/WorkspacePage.h"
#include "projects/ProjectFlows.h"
#include "tasks/TaskDrag.h"
#include "tasks/TaskPresenter.h"
#include "tasks/TaskReadoutSource.h"
#include "tasks/TaskWindow.h"
#include "workspaces/FloatingViewChrome.h"
#include "workspaces/RenameWorkspaceDialog.h"
#include "workspaces/WorkspaceHost.h"
#include "workspaces/WorkspaceToolbar.h"
#include "workspaces/WorkspaceWindow.h"

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
#include "projects/ProjectLayoutStore.h"
#include "projects/ProjectService.h"
#include "readout/ReadoutController.h"
#include "services/AppEventBus.h"
#include "services/DialogService.h"
#include "services/ExtensionService.h"
#include "services/FileDropHandler.h"
#include "services/OpenProcessRegistry.h"
#include "services/ProcessLaunchService.h"
#include "services/RecentFilesModel.h"
#include "services/WindowGeometryStore.h"
#include "tasks/TaskModel.h"
#include "views/DockHooks.h"
#include "views/DockLayoutOperations.h"
#include "views/ViewService.h"

#include "app/AfterNightAppListener.h"
#include "app/AppLifecycleCoordinator.h"
#include "app/ChannelExtractor.h"
#include "app/DialogOrchestrationCoordinator.h"
#include "app/FileManager.h"
#include "app/FileWorkflowRunner.h"
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
#include "components/AnCompactReadout.h"
#include "components/AnFloatingToolbar.h"
#include "components/AnHistoryStrip.h"
#include "components/AnIconButton.h"
#include "components/AnIconRail.h"
#include "components/AnReadoutColumn.h"
#include "components/AnSnackbar.h"
#include "components/AnTaskHeader.h"
#include "components/RailItem.h"
#include "controls/ExportSequenceControl.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/ImageRTPreviewControl.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/KDDockWidgetsHelper.h"
#include "controls/ViewChrome.h"
#include "controls/NewSequenceControl.h"
#include "controls/ProcessPageLayout.h"
#include "controls/ProgressDialogControl.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "theme/AnProxyStyle.h"
#include "theme/ComponentParts.h"
#include "theme/ObservatoryDock.h"

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/TabBar.h>

#include <QAccessible>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMimeData>
#include <QTimer>
#include <QMenu>
#include <QPointer>
#include <QPainter>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QScreen>
#include <QShowEvent>
#include <QToolButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace an::ui::observatory
{

namespace
{

/// The Image Integration process, which Home's quick action opens.
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

/// The overlay laid over the zone a dragged task's window would attach to: the accent's soft share inside a 2 px accent
/// border, as the band over the place a dragged view would land.
class AttachZoneOverlay final : public QWidget
{
public:
    explicit AttachZoneOverlay(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("observatoryAttachZone"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        hide();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        an::ui::paintObservatoryDropBand(painter, rect());
    }
};

} // namespace

/// What a task's drag reads of the window, and does to it.
class ObservatoryWindow::DragSurface final : public TaskDrag::Surface
{
public:
    explicit DragSurface(ObservatoryWindow& window) : m_window(window) {}

    [[nodiscard]] QRect frame() const override;
    [[nodiscard]] QRect rail() const override;
    [[nodiscard]] QRect centre() const override;
    [[nodiscard]] bool windowFollowsTheCursor() const override;
    bool floatUnder(an::ui::shell::TaskId id, QPoint globalPos, bool follow) override;
    void attach(an::ui::shell::TaskId id) override;
    void showAttachZone(AttachZone zone, an::ui::shell::TaskId id) override;
    void endRailDrag() override;

private:
    ObservatoryWindow& m_window;
};

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
    // The view's menu, which the More of every workspace page's toolbar opens.
    an::ui::shell::ViewChromeBinder::MenuFiller viewMenuFiller;
    // The workspaces moved to windows of their own, by their ids, and what the window hears of the workspaces.
    std::map<QString, QPointer<WorkspaceWindow>> workspaceWindows;
    std::size_t workspaceSubscription = 0;
    bool closingWorkspaces = false;
    WorkspaceToolbar* layoutIcons = nullptr;
    FloatingViewChrome* floatingChrome = nullptr;
    std::unique_ptr<WorkspaceHost> workspaces;
    // The window's chrome follows the active view through the core's listeners.
    std::unique_ptr<ViewFollower> follower;
    // The shell's process presenter: the tasks, what their views show of them.
    std::unique_ptr<TaskPresenter> tasks;
    // The windows of the floating tasks; each floating page's compact readout, which follows its preview; the
    // processes' names, whose settings keep a window's size; what decides the tasks' drags, and the overlay of an
    // attach zone.
    struct FloatingReadout
    {
        std::unique_ptr<TaskReadoutSource> source;
        std::unique_ptr<an::ui::shell::ReadoutController> controller;
    };
    std::map<an::ui::shell::TaskId, std::unique_ptr<TaskWindow>> taskWindows;
    std::map<an::ui::shell::TaskId, FloatingReadout> floatingReadouts;
    std::map<an::ui::shell::TaskId, QString> floatingProcessNames;
    std::unique_ptr<DragSurface> dragSurface;
    std::unique_ptr<TaskDrag> taskDrag;
    an::ui::shell::DockHooks::Subscription taskDragHooks;
    QWidget* attachZone = nullptr;
    // The pages of the tasks taken into one, by their tasks; the task whose page the centre shows, and the place it
    // showed before the first task's page.
    std::map<an::ui::shell::TaskId, QPointer<TaskPage>> taskPages;
    an::ui::shell::TaskId shownTask = 0;
    QPointer<QWidget> placeBeforeTask;
    // What the compact readout of the task shown reads.
    std::unique_ptr<TaskReadoutSource> readoutSource;
    // When each task's run began, and when its last one succeeded: the snackbar's detail and the header's state.
    std::map<an::ui::shell::TaskId, std::chrono::steady_clock::time_point> runStarted;
    std::map<an::ui::shell::TaskId, std::chrono::steady_clock::time_point> appliedAt;
    QTimer appliedTicker;
    // Under Wayland a task's window is dragged as a QDrag: where the pointer was released over this window, the
    // platform the drags decide by when a suite says one, and the leave a move to another widget sends before its
    // enter.
    std::optional<QPoint> taskDropAt;
    QString dragPlatform;
    QTimer taskDragLeave;
    // The transient message at the foot of the centre, and what its action does.
    an::ui::AnSnackbar* snackbar = nullptr;
    std::function<void()> snackbarAction;
    // What the window does about projects, and whether what the start's restore came to was said.
    std::unique_ptr<ProjectFlows> projects;
    bool projectRestoreSaid = false;
    // What a project keeps of the workspaces and of the images open in them.
    std::unique_ptr<an::ui::shell::ProjectLayoutStore> layout;
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

    // What the window's units read through a pointer to a pointer: the dock area, the lock's button (this window has
    // none).
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

    // The Session Console's drawer, and whether what it does is the last start's being put back, which is not kept
    // again.
    ConsoleDrawer* consoleDrawer = nullptr;
    bool restoringConsole = false;
    bool consoleRestored = false;

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

// ---- What a task's drag reads and does

QRect ObservatoryWindow::DragSurface::frame() const
{
    if (!windowFollowsTheCursor())
    {
        // Where the window cannot be placed its frame is not where the drag's points are read: they are this window's
        // own, as the rail's and the centre's are.
        return QRect(m_window.mapToGlobal(QPoint(0, 0)), m_window.size());
    }
    return m_window.frameGeometry();
}

QRect ObservatoryWindow::DragSurface::rail() const
{
    const QWidget* const rail = m_window.m_impl->rail;
    return QRect(rail->mapToGlobal(QPoint(0, 0)), rail->size());
}

QRect ObservatoryWindow::DragSurface::centre() const
{
    const QWidget* const centre = m_window.m_impl->centre;
    return QRect(centre->mapToGlobal(QPoint(0, 0)), centre->size());
}

bool ObservatoryWindow::DragSurface::windowFollowsTheCursor() const
{
    const QString& named = m_window.m_impl->dragPlatform;
    return an::ui::observatory::windowFollowsTheCursor(named.isEmpty() ? QGuiApplication::platformName() : named);
}

bool ObservatoryWindow::DragSurface::floatUnder(const an::ui::shell::TaskId id, const QPoint globalPos,
                                                const bool follow)
{
    const an::ui::shell::Task* const task = m_window.m_impl->tasks ? m_window.m_impl->tasks->model().task(id) : nullptr;
    if (task == nullptr)
    {
        return false;
    }
    // The window's top under the cursor, within its title.
    const QSize size = m_window.floatingSizeOf(*task);
    const QRect geometry(globalPos - QPoint(size.width() / 2, TaskWindow::kGripOffset), size);
    if (!m_window.floatTaskAt(id, geometry) || !follow)
    {
        return false;
    }
    // The press goes on as the dock library's drag of the window.
    TaskWindow* const window = m_window.taskWindow(id);
    return window != nullptr && window->dockWidget()->dockWidget()->startDragging();
}

void ObservatoryWindow::DragSurface::attach(const an::ui::shell::TaskId id)
{
    // The dock library is still ending its drag of the window: the page leaves it once the library is done.
    QPointer<ObservatoryWindow> window(&m_window);
    QTimer::singleShot(0, &m_window, [window, id]() {
        if (window)
        {
            (void)window->attachTask(id);
        }
    });
}

void ObservatoryWindow::DragSurface::showAttachZone(const AttachZone zone, const an::ui::shell::TaskId id)
{
    m_window.showAttachZone(static_cast<int>(zone), id);
}

void ObservatoryWindow::DragSurface::endRailDrag()
{
    m_window.m_impl->rail->endDrag();
}

// ---- The window

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

    // The tasks are kept from the window's start: a process opened from here on is one.
    impl.tasks = std::make_unique<TaskPresenter>(core);

    buildRegions();
    makeUnits();
    bindServices();

    TaskPresenter::Hooks taskHooks;
    taskHooks.tasksChanged = [this]() {
        refreshTaskPages();
        updateRail();
        refreshPendingChips();
        updateTaskActions();
    };
    taskHooks.viewChanged = [this](ImageViewControl* /*view*/) { refreshPendingChips(); };
    taskHooks.adoptPage = [this](TopLevelProcessBaseControl& process) { return adoptTaskPage(process); };
    taskHooks.showPage = [this](const an::ui::shell::TaskId id) { showTaskPage(id); };
    taskHooks.showWindow = [this](const an::ui::shell::TaskId id) {
        if (TaskWindow* const floating = taskWindow(id))
        {
            floating->raiseAndActivate();
        }
    };
    taskHooks.runChanged = [this](const an::ui::shell::TaskId id, const an::ui::ProcessRunState& before) {
        taskRunChanged(id, before);
    };
    impl.tasks->setHooks(std::move(taskHooks));

    // A task's drags: of its thumbnail out of the rail, and of its window by its title bar, which the dock library
    // tells.
    impl.dragSurface = std::make_unique<DragSurface>(*this);
    impl.taskDrag = std::make_unique<TaskDrag>(*impl.dragSurface);
    impl.taskDrag->followRail(*impl.rail, [](const QString& itemId) {
        const QString prefix = railTaskId(0u).chopped(1);
        bool numbered = false;
        const an::ui::shell::TaskId task =
            itemId.startsWith(prefix) ? itemId.mid(prefix.size()).toULongLong(&numbered) : 0u;
        return numbered ? task : an::ui::shell::TaskId{0};
    });
    an::ui::shell::DockHooks::Subscriber dragHooks;
    dragHooks.dragAboutToStart = [this](const an::ui::shell::DockDrag& drag) {
        if (const an::ui::shell::TaskId id = taskOfDock(drag.dockWidget); id != 0u && m_impl->taskDrag)
        {
            m_impl->taskDropAt.reset();
            m_impl->taskDrag->windowDragStarted(id);
        }
        return true;
    };
    dragHooks.dragEnded = [this](const an::ui::shell::DockDrag& drag) {
        const an::ui::shell::TaskId id = taskOfDock(drag.dockWidget);
        if (id == 0u || !m_impl->taskDrag || m_impl->taskDrag->task() != id)
        {
            return;
        }
        m_impl->taskDragLeave.stop();
        if (m_impl->dragSurface->windowFollowsTheCursor())
        {
            m_impl->taskDrag->windowDragEnded(QCursor::pos());
        }
        else if (const std::optional<QPoint> dropped = std::exchange(m_impl->taskDropAt, std::nullopt))
        {
            // Under Wayland the cursor's place is not the application's to read: the drop said where it ended.
            m_impl->taskDrag->windowDragEnded(*dropped);
        }
        else
        {
            m_impl->taskDrag->windowDragCancelled();
        }
    };
    impl.taskDragHooks = an::ui::shell::DockHooks::subscribe(std::move(dragHooks));
    impl.taskDragLeave.setSingleShot(true);
    impl.taskDragLeave.setInterval(0);
    QObject::connect(&impl.taskDragLeave, &QTimer::timeout, this, [this]() {
        if (m_impl->taskDrag)
        {
            m_impl->taskDrag->windowDragLeft();
        }
    });
    // "applied 2.8 s ago" is told again each second while a task's page is shown, or floats.
    impl.appliedTicker.setInterval(1000);
    QObject::connect(&impl.appliedTicker, &QTimer::timeout, this, [this]() {
        if (m_impl->shownTask != 0u)
        {
            refreshTaskHeader(m_impl->shownTask);
        }
        for (const auto& [id, floating] : m_impl->taskWindows)
        {
            refreshTaskHeader(id);
        }
    });
    // The task whose window is active is the one the person looks at.
    QObject::connect(qApp, &QGuiApplication::focusWindowChanged, this, [this](QWindow* /*focus*/) {
        if (m_impl->tasks)
        {
            m_impl->tasks->activeWindowChanged(QApplication::activeWindow());
        }
    });

    // The installed extensions joined the process menus as the application started; the catalogue's actions are
    // connected to what they do, the process actions bound over the registry.
    core.actionBindings().connectProcessActions();
    core.actionBindings().connectActions();
    bindShellActions();

    // What a project keeps of the workspaces: written as the project is left, put back as it is opened.
    an::ui::shell::ProjectLayoutStore::Hooks layoutHooks;
    layoutHooks.dockTarget = [this](const QString& id) { return m_impl->workspaces->target(id); };
    layoutHooks.setWorkspaces = [this](const std::vector<an::ui::shell::ProjectWorkspace>& listed) {
        takeProjectWorkspaces(listed);
    };
    layoutHooks.openFiles = [this](const QStringList& files, std::function<void()> done) {
        openProjectImages(files, std::move(done));
    };
    layoutHooks.setLocked = [this](const QString& id, const bool locked) { lockWorkspace(id, locked); };
    layoutHooks.dockView = [this](ImageViewControl& view, const QString& id) { dockRestoredView(view, id); };
    layoutHooks.restored = [this](const an::ui::shell::ProjectLayoutRestored& restored) {
        projectLayoutRestored(restored);
    };
    impl.layout =
        std::make_unique<an::ui::shell::ProjectLayoutStore>(core, impl.workspaces->model(), std::move(layoutHooks));
    // A project is left through the window: its arrangement kept, its images closed.
    core.projects().setLeaveGuard([this]() { return leaveProject(); });

    // Projects: the shell's project actions, the top bar's switcher and what a refusal is said as.
    ProjectFlows::Hooks projectHooks;
    projectHooks.openImages = [this]() {
        std::vector<ProjectFlows::OpenImage> open;
        for (ImageViewControl* const view : m_impl->core.viewService().getListOfViewControls())
        {
            if (view == nullptr || view->getViewControlMode() != ViewControlMode::standard)
            {
                continue;
            }
            // An image of the project that is open closes with it: it is not one a new project can take.
            if (!m_impl->core.projectHistory().imageOf(*view).isEmpty())
            {
                continue;
            }
            const std::string file = view->getFileName();
            if (!file.empty() && !m_impl->core.isTempFile(file))
            {
                open.push_back(ProjectFlows::OpenImage{view, QString::fromStdString(file), view->isSequence()});
            }
        }
        return open;
    };
    projectHooks.snackbar = [this](const QString& text, const QString& detail, const QString& action,
                                   std::function<void()> act) { showSnackbar(text, detail, action, std::move(act)); };
    projectHooks.openSettingsAt = [this](const QString& pageTitle) { openSettingsAt(pageTitle); };
    projectHooks.restoreArrangement = [this]() { return m_impl->layout && m_impl->layout->restore(); };
    projectHooks.openImage = [this](const QString& file, const QString& imageId) { openProjectImage(file, imageId); };
    impl.projects =
        std::make_unique<ProjectFlows>(*this, core, *impl.shellActions, *impl.topBar, std::move(projectHooks));
}

ObservatoryWindow::~ObservatoryWindow()
{
    Impl& impl = *m_impl;
    an::ui::shell::ApplicationCore& core = impl.core;

    qApp->removeEventFilter(this);
    // The project that is open is asked nothing more of this window; the projects' unit lets go of the top bar and of
    // the project service before either goes, and what keeps the arrangement of the workspaces before they do.
    core.projects().setLeaveGuard({});
    impl.projects.reset();
    impl.layout.reset();
    // The task shown is let go of: the readout reads no process, the actions act on no page.
    impl.appliedTicker.stop();
    if (impl.readout)
    {
        impl.readout->followTask(nullptr);
        impl.readout->setCompactReadout(nullptr);
    }
    impl.readoutSource.reset();
    // The floating pages' readouts let go of their processes, and the drags of the windows.
    for (auto& [id, floating] : impl.floatingReadouts)
    {
        if (floating.controller)
        {
            floating.controller->followTask(nullptr);
            floating.controller->setCompactReadout(nullptr);
        }
        floating.controller.reset();
        floating.source.reset();
    }
    impl.floatingReadouts.clear();
    impl.taskDragHooks.reset();
    impl.taskDrag.reset();
    impl.dragSurface.reset();
    impl.shownTask = 0;
    core.viewService().clearTaskSubject();
    // The tasks go first: what the process windows tell as they go reaches no presenter.
    if (impl.tasks)
    {
        impl.tasks->setHooks({});
        impl.tasks.reset();
    }
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

    // The Session Console goes while the window's state is still there, its window of its own with it.
    delete impl.consoleDrawer;
    impl.consoleDrawer = nullptr;
    // The workspaces in windows of their own come back: their pages go with this window.
    if (impl.workspaces)
    {
        impl.workspaces->model().unsubscribe(impl.workspaceSubscription);
        for (auto& [id, holder] : impl.workspaceWindows)
        {
            if (holder.isNull())
            {
                continue;
            }
            if (holder->takePage() != nullptr)
            {
                impl.workspaces->attach(id);
            }
            holder->closeWithoutAsking();
            delete holder.data();
        }
        impl.workspaceWindows.clear();
    }
    core.extensions().cancelBackgroundWork();
    core.extensions().stopSessions();

    // The process windows go before the stores they read: each stops what it still runs, the thread pool is drained
    // while every window is still there, only then are they deleted.
    const std::vector<TopLevelProcessBaseControl*> open = core.openProcesses().snapshot();
    std::vector<QPointer<TopLevelProcessBaseControl>> openProcesses(open.begin(), open.end());
    an::ui::destroyProcessWindowsAtExit(openProcesses);
    // A floating task's window goes after its process, as a process window's own window would: the process was stopped,
    // the work drained and the process deleted while the window still held it.
    impl.taskWindows.clear();
    impl.floatingProcessNames.clear();

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

    // The Session Console's drawer, between the centre and the status line, spanning the centre and the column.
    ConsoleDrawer::Hooks drawerHooks;
    drawerHooks.changed = [this]() { consoleChanged(); };
    drawerHooks.unseenChanged = [this](const int unseen) { consoleUnseen(unseen); };
    drawerHooks.icon = [this](const QString& name) { return m_impl->icon(name.toStdString()); };
    impl.consoleDrawer = new ConsoleDrawer(*this, std::move(drawerHooks), central);
    right->addWidget(impl.consoleDrawer);
    {
        // Its height is the last start's; whether it is open, and floats, is put back once the window is shown.
        const QScopedValueRollback<bool> restoring(impl.restoringConsole, true);
        bool readable = false;
        const int height = impl.core.globalSettings()
                               ->select(QString::fromLatin1(SettingsKeys::INTERFACE_CONSOLE_DRAWER_HEIGHT),
                                        QString::number(SettingsDefaults::INTERFACE_CONSOLE_DRAWER_HEIGHT))
                               .toInt(&readable);
        impl.consoleDrawer->setDrawerHeight(readable ? height : SettingsDefaults::INTERFACE_CONSOLE_DRAWER_HEIGHT);
    }

    impl.statusLine = new StatusLine(central);
    right->addWidget(impl.statusLine);

    setCentralWidget(central);

    // The transient message, at the foot of the centre over everything there.
    impl.snackbar = new an::ui::AnSnackbar(central);
    impl.snackbar->setObjectName(QStringLiteral("observatorySnackbar"));
    impl.snackbar->hide();
    // The zone a dragged task's window would attach to, over everything there.
    impl.attachZone = new AttachZoneOverlay(central);
    QObject::connect(impl.snackbar, &an::ui::AnSnackbar::actionTriggered, this, [this]() {
        if (const std::function<void()> act = m_impl->snackbarAction)
        {
            act();
        }
    });

    // Home: Controls' Home parts on a page.
    impl.homePage = new HomePage(impl.centre);
    impl.centre->addWidget(impl.homePage);

    // The view's menu fills the toolbars' More: the menus it shares are the menu bar's.
    impl.viewMenuFiller = [this](QMenu& menu, ImageViewControl& view) {
        an::ui::shell::ViewMenuBuilder(m_impl->core).buildViewMenu(menu, view, [this](std::string_view menuId) {
            return m_impl->menuBar->menu(menuId);
        });
    };
    // The workspaces: a dock area each, made with this window as its parent and handed to the page made for it, on
    // the stack that is the workspaces' place.
    WorkspaceHost::Hooks workspaceHooks;
    workspaceHooks.arrangementChanged = [this]() { arrangementChanged(); };
    workspaceHooks.dragStarting = [this]() { showDockAreasForADrag(); };
    workspaceHooks.makePage = [this](const QString& id, KDDockWidgets::QtWidgets::MainWindow& dockArea) {
        return makeWorkspacePage(id, dockArea);
    };
    impl.workspaces = std::make_unique<WorkspaceHost>(*this, std::move(workspaceHooks));
    impl.dockArea = impl.workspaces->mainWindow();
    impl.centre->addWidget(impl.workspaces->stack());
    impl.workspaceSubscription =
        impl.workspaces->model().subscribe([this](const an::ui::shell::WorkspaceModel::Change change,
                                                  const QString& id) { workspacesChanged(change, id); });
    impl.floatingChrome = new FloatingViewChrome(this);
    impl.floatingChrome->setMenuFiller(impl.viewMenuFiller);

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
    connectHome();

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

    // The rail: the places - Home, a workspace each, Integration - and at its foot the Session Console and Settings;
    // a workspace's icon opens its menu.
    QObject::connect(impl.rail, &an::ui::AnIconRail::itemMenuRequested, this,
                     [this](const QString& id, const QPoint& globalPos) {
                         const QString taskPrefix = railTaskId(0u).chopped(1);
                         if (id.startsWith(taskPrefix))
                         {
                             bool numbered = false;
                             const an::ui::shell::TaskId task = id.mid(taskPrefix.size()).toULongLong(&numbered);
                             if (!numbered)
                             {
                                 return;
                             }
                             auto* menu = new QMenu(this);
                             menu->setObjectName(QStringLiteral("taskMenu"));
                             menu->setAttribute(Qt::WA_DeleteOnClose);
                             buildTaskMenu(*menu, task);
                             menu->popup(globalPos);
                             return;
                         }
                         const QString prefix = railWorkspaceId(QString());
                         if (!id.startsWith(prefix))
                         {
                             return;
                         }
                         auto* menu = new QMenu(this);
                         menu->setObjectName(QStringLiteral("workspaceMenu"));
                         menu->setAttribute(Qt::WA_DeleteOnClose);
                         buildWorkspaceMenu(*menu, id.mid(prefix.size()));
                         menu->popup(globalPos);
                     });
    placeChanged();
}

void ObservatoryWindow::connectHome()
{
    // Home asks in the dashboard's signals; the window answers as it answered the dashboard.
    HomePage* const home = m_impl->homePage;
    QObject::connect(home, &HomePage::openFileRequested, this, [this]() { openFile(); });
    QObject::connect(home, &HomePage::newSequenceRequested, this, [this]() { newSequence(); });
    QObject::connect(home, &HomePage::imageIntegrationRequested, this, [this]() { openImageIntegration(); });
    QObject::connect(home, &HomePage::newProjectRequested, this, [this]() {
        if (QAction* const action = m_impl->shellActions->action(ids::kNewProject))
        {
            action->trigger();
        }
    });
    QObject::connect(home, &HomePage::openExtensionManagerRequested, this,
                     [this]() { openSettingsAt(an::ui::shell::ExtensionService::managerPageTitle()); });
    QObject::connect(home, &HomePage::clearRecentFilesRequested, this, [this]() {
        if (m_impl->fileManager)
        {
            m_impl->fileManager->clearRecentFilesList();
        }
    });
    QObject::connect(home, &HomePage::systemInformationRequested, this, [this]() {
        if (m_impl->dialogs)
        {
            m_impl->dialogs->systemInformation();
        }
    });
    QObject::connect(home, &HomePage::openHelpRequested, this, [this]() {
        if (m_impl->dialogs)
        {
            m_impl->dialogs->openHelp();
        }
    });
    QObject::connect(home, &HomePage::openRecentFileRequested, this, [this](const QString& filePath) {
        if (m_impl->fileManager)
        {
            m_impl->fileManager->openRecentSingleFile(filePath);
        }
    });
    QObject::connect(home, &HomePage::removeRecentFileRequested, this, [this](const QString& filePath) {
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
        if (m_impl->homePage != nullptr)
        {
            m_impl->homePage->reloadRecentFiles();
        }
    };
    fileDeps.notifyRecentFilesListDeleted = [this]() {
        if (m_impl->homePage != nullptr)
        {
            m_impl->homePage->recentFilesCleared();
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
    // On a task's page the image operations act on its target and the looking ones on its canvas.
    imageDeps.activeViewControl = [this]() -> ImageViewControl* { return m_impl->core.viewService().subjectView(); };
    imageDeps.lookingViewControl = [this]() -> ImageViewControl* {
        return m_impl->core.viewService().hasTaskSubject() ? m_impl->core.viewService().lookingView() : nullptr;
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
    // This shell offers projects: the Settings show their page.
    dialogDeps.offersProjects = true;
    dialogDeps.updateRecentFilesMenuEntries = [this]() {
        if (m_impl->fileManager)
        {
            m_impl->fileManager->updateRecentFilesMenuEntries();
        }
        // The settings changed the recent files: Home reads them again.
        if (m_impl->homePage != nullptr)
        {
            m_impl->homePage->reloadRecentFiles();
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
    lifecycleDeps.notifyApplicationClose = [this]() {
        // The application ends, and the person agreed to it: the open project keeps the workspaces as they are now,
        // before the views go. Nothing is written with no project.
        if (m_impl->layout)
        {
            (void)m_impl->layout->save();
        }
        m_impl->core.eventBus().notifyApplicationClose();
    };
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
    targets.fitAll = [this]() { m_impl->core.dockLayout().fitAllToWindow(dockTarget()); };
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
    // The project used last, when the settings ask for it: opened before the window shows.
    if (m_impl->projects)
    {
        m_impl->projects->restoreLastProject();
    }
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

ConsoleDrawer* ObservatoryWindow::consoleDrawer() const noexcept
{
    return m_impl->consoleDrawer;
}

StatusLine* ObservatoryWindow::statusLine() const noexcept
{
    return m_impl->statusLine;
}

HomePage* ObservatoryWindow::homePage() const noexcept
{
    return m_impl->homePage;
}

QString ObservatoryWindow::railTaskId(const an::ui::shell::TaskId id)
{
    return QStringLiteral("task.%1").arg(id);
}

TaskPresenter* ObservatoryWindow::taskPresenter() const noexcept
{
    return m_impl->tasks.get();
}

QString ObservatoryWindow::railWorkspaceId(const QString& workspaceId)
{
    return QStringLiteral("workspace.") + workspaceId;
}

WorkspacePage* ObservatoryWindow::workspacePage() const noexcept
{
    return m_impl->workspaces->page(m_impl->workspaces->shown());
}

WorkspaceWindow* ObservatoryWindow::workspaceWindow(const QString& workspaceId) const
{
    const auto found = m_impl->workspaceWindows.find(workspaceId);
    return found != m_impl->workspaceWindows.end() ? found->second.data() : nullptr;
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

ProjectFlows& ObservatoryWindow::projectFlows() const noexcept
{
    return *m_impl->projects;
}

an::ui::shell::ProjectLayoutStore& ObservatoryWindow::projectLayout() const noexcept
{
    return *m_impl->layout;
}

bool ObservatoryWindow::continueProject()
{
    Impl& impl = *m_impl;
    const QString last = impl.layout ? impl.layout->lastWorkspace() : QString();
    if (last.isEmpty())
    {
        return false;
    }
    showWorkspace(last);
    return true;
}

QString ObservatoryWindow::continueLabel() const
{
    const Impl& impl = *m_impl;
    const QString last = impl.layout ? impl.layout->lastWorkspace() : QString();
    const QString name = impl.workspaces->workspaceName(last);
    return name.isEmpty() ? QString() : tr("Continue") + QStringLiteral(" \u00b7 ") + name;
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
    return m_impl->centre->currentWidget() == m_impl->workspaces->stack();
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
    return m_impl->consoleDrawer != nullptr && m_impl->consoleDrawer->isOpen();
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
    Impl& impl = *m_impl;
    // A view maximized goes back to the dock area, which the new view joins.
    if (WorkspacePage* const page = impl.workspaces->currentPage())
    {
        if (ImageViewControl* const maximized = page->maximizedView();
            maximized != nullptr && maximized != &view && !maximized->isInFloatingWindow())
        {
            maximized->switchMaximized();
        }
    }
    adoptView(view);
    // A view a project's restore asked for is left undocked: the saved arrangement places it once every one is open.
    if (impl.layout && impl.layout->takes(view))
    {
        return;
    }
    // Beside its neighbour, in the neighbour's workspace; with none, in the workspace on screen.
    an::ui::dockCreatedView(view, placement.neighbour, impl.dockArea);
    const QString workspace = impl.workspaces->workspaceOf(view);
    if (isTaskPageShown() && placement.origin == an::ui::shell::ViewOrigin::Process)
    {
        updateWorkspacePage();
        const QString where =
            impl.workspaces->workspaceName(workspace.isEmpty() ? impl.workspaces->current() : workspace);
        QPointer<ImageViewControl> made(&view);
        showSnackbar(tr("%1 opened in %2").arg(QString::fromUtf8(view.getUniqueName()), where), QString(), tr("Show"),
                     [this, made]() {
                         if (made.isNull())
                         {
                             return;
                         }
                         const QString in = m_impl->workspaces->workspaceOf(*made);
                         if (in.isEmpty())
                         {
                             showViews();
                         }
                         else
                         {
                             showWorkspace(in);
                         }
                         m_impl->core.viewService().setActiveImageViewControl(made.data());
                     });
        return;
    }
    if (workspace.isEmpty())
    {
        showViews();
    }
    else
    {
        showWorkspace(workspace);
    }
    updateWorkspacePage();
}

void ObservatoryWindow::adoptView(ImageViewControl& view)
{
    // The view's tools are outside it in this shell: its workspace's floating toolbar and history strip.
    view.setChromeMode(an::ui::ViewChromeMode::External);
    // A view floats or docks by a drag, a double click or its strip's button: what the workspace shows follows, once
    // the dock library has moved it.
    if (KDDockWidgets::QtWidgets::DockWidget* const dock = view.getDockWidget())
    {
        QObject::connect(dock, &KDDockWidgets::QtWidgets::DockWidget::isFloatingChanged, this,
                         [this](bool) { QTimer::singleShot(0, this, [this]() { updateWorkspacePage(); }); });
    }
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

void ObservatoryWindow::viewAdded(ImageViewControl& view, const an::ui::shell::ViewOrigin /*origin*/)
{
    if (m_impl->tasks)
    {
        m_impl->tasks->watch(view);
    }
    // While a project is open, an image opened from a file is the project's, and its undo history is kept there;
    // an image saved while one is open joins it then. The shell core hears the view from here on.
    m_impl->core.projectHistory().watch(view);
    refreshViewSelector();
    placeChanged();
}

void ObservatoryWindow::viewRemoved(ImageViewControl& view)
{
    Impl& impl = *m_impl;
    for (const QString& id : impl.workspaces->ids())
    {
        WorkspacePage* const page = impl.workspaces->page(id);
        if (page == nullptr)
        {
            continue;
        }
        if (page->maximizedView() == &view)
        {
            page->restore(view);
        }
        if (page->binder().toolbarView() == &view || page->binder().historyView() == &view)
        {
            page->bindView(nullptr);
        }
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
    WorkspacePage* const page = pageOf(view);
    if (page == nullptr)
    {
        return;
    }
    page->maximize(view);
    // The layout of the view's workspace is locked while a view is maximized: its dock keeps a stand-in that must stay
    // where it is.
    const an::ui::shell::DockTarget target = dockTarget(&view);
    if (!impl.core.dockLayout().isLocked(target))
    {
        impl.unlockWhenRestored = true;
        impl.core.dockLayout().lock(target);
    }
    showWorkspace(impl.workspaces->workspaceOf(view));
    impl.core.viewService().setActiveImageViewControl(&view);
    offerTheLock(true);
}

void ObservatoryWindow::restore(ImageViewControl& view)
{
    Impl& impl = *m_impl;
    WorkspacePage* const page = pageOf(view);
    if (page == nullptr)
    {
        return;
    }
    page->restore(view);
    if (page->maximizedView() == nullptr && impl.unlockWhenRestored)
    {
        impl.unlockWhenRestored = false;
        impl.core.dockLayout().unlock(dockTarget(&view));
    }
    impl.core.viewService().setActiveImageViewControl(&view);
    offerTheLock(page->maximizedView() != nullptr);
}

bool ObservatoryWindow::isMaximizedViewActive() const
{
    const WorkspacePage* const page = workspacePage();
    return isWorkspaceShown() && page != nullptr && page->maximizedView() != nullptr;
}

an::ui::shell::DockTarget ObservatoryWindow::dockTarget(const ImageViewControl* view)
{
    // A view docked in a workspace is that workspace's; any other target is the current workspace's.
    if (view != nullptr)
    {
        const QString workspace = m_impl->workspaces->workspaceOf(const_cast<ImageViewControl&>(*view));
        if (!workspace.isEmpty())
        {
            return m_impl->workspaces->target(workspace);
        }
    }
    return m_impl->workspaces->target();
}

void ObservatoryWindow::showHome()
{
    m_impl->centre->setCurrentWidget(m_impl->homePage);
    placeChanged();
}

void ObservatoryWindow::showViews()
{
    Impl& impl = *m_impl;
    // The workspaces' place shows the workspace last shown in this window; with every one in a window of its own, Home
    // is the place.
    if (impl.workspaces->shown().isEmpty())
    {
        showHome();
        return;
    }
    impl.centre->setCurrentWidget(impl.workspaces->stack());
    placeChanged();
}

// ---- The status line

void ObservatoryWindow::showImageInfo(ImageViewControl* view)
{
    // On a task's page the status line shows the preview's facts.
    ImageViewControl* const looking = isTaskPageShown() ? m_impl->core.viewService().lookingView() : nullptr;
    m_impl->statusLine->showImageInfo(looking != nullptr ? looking : view);
}

void ObservatoryWindow::showZoom(ImageViewControl* view)
{
    ImageViewControl* const looking = isTaskPageShown() ? m_impl->core.viewService().lookingView() : nullptr;
    m_impl->statusLine->showZoom(looking != nullptr ? looking : view);
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
    switch (event->type())
    {
    case QEvent::DragEnter:
    case QEvent::DragMove:
    case QEvent::DragLeave:
    case QEvent::Drop:
        if (taskWindowDragEvent(watched, event))
        {
            return true;
        }
        break;
    default:
        break;
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
    if (event->type() == QEvent::KeyPress && workspacePage() != nullptr && workspacePage()->maximizedView() != nullptr)
    {
        const auto* key = static_cast<QKeyEvent*>(event);
        const auto* widget = qobject_cast<QWidget*>(watched);
        if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier && widget != nullptr &&
            widget->window() == this && QApplication::activePopupWidget() == nullptr &&
            QApplication::activeModalWidget() == nullptr)
        {
            workspacePage()->maximizedView()->switchMaximized();
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
    if (m_impl->consoleDrawer != nullptr)
    {
        m_impl->consoleDrawer->windowResized();
    }
    placePaletteScrim();
    placeSnackbar();
}

void ObservatoryWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    // This window active again, its workspace on screen is the current one: what the layout's actions act on.
    if (event != nullptr && event->type() == QEvent::ActivationChange && isActiveWindow() && m_impl->workspaces &&
        !m_impl->workspaces->shown().isEmpty() && m_impl->workspaces->current() != m_impl->workspaces->shown())
    {
        m_impl->workspaces->setCurrent(m_impl->workspaces->shown());
    }
}

void ObservatoryWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    // The drawer as the last start left it, once the window is on screen and laid out.
    if (!m_impl->consoleRestored)
    {
        m_impl->consoleRestored = true;
        QTimer::singleShot(0, this, [this]() { restoreConsole(); });
    }
    // A project the start could not open is said once the window can say it.
    if (!m_impl->projectRestoreSaid && m_impl->projects)
    {
        m_impl->projectRestoreSaid = true;
        QTimer::singleShot(0, this, [this]() {
            if (m_impl->projects)
            {
                m_impl->projects->sayRestoreRefusal();
            }
        });
    }
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
    if (m_impl->homePage != nullptr)
    {
        m_impl->homePage->refreshIcons();
    }
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
    if (m_impl->consoleDrawer != nullptr)
    {
        m_impl->consoleDrawer->setOpen(visible);
    }
}

void ObservatoryWindow::consoleChanged()
{
    Impl& impl = *m_impl;
    const ConsoleDrawer& drawer = *impl.consoleDrawer;
    if (QAction* const action = impl.core.actions().action(an::ui::shell::ids::kLogViewer);
        action != nullptr && action->isChecked() != drawer.isOpen())
    {
        const QSignalBlocker blocker(action);
        action->setChecked(drawer.isOpen());
    }
    if (impl.restoringConsole)
    {
        return;
    }
    an::ui::IGlobalSettingsProvider* const settings = impl.core.globalSettings();
    const auto keep = [settings](const char* key, const QString& value) {
        if (settings->select(QString::fromLatin1(key), QString()) != value)
        {
            (void)settings->upsert(QString::fromLatin1(key), value);
        }
    };
    keep(SettingsKeys::INTERFACE_CONSOLE_DRAWER_OPEN, drawer.isOpen() ? QStringLiteral("1") : QStringLiteral("0"));
    keep(SettingsKeys::INTERFACE_CONSOLE_DRAWER_HEIGHT, QString::number(drawer.drawerHeight()));
    keep(SettingsKeys::INTERFACE_CONSOLE_FLOATING, drawer.isFloating() ? QStringLiteral("1") : QStringLiteral("0"));
}

void ObservatoryWindow::consoleUnseen(const int unseen)
{
    Impl& impl = *m_impl;
    const QString count = unseen > 0 ? QString::number(unseen) : QString();
    updateRail();
    impl.statusLine->consoleChip()->setCountText(count);
}

void ObservatoryWindow::restoreConsole()
{
    Impl& impl = *m_impl;
    an::ui::IGlobalSettingsProvider* const settings = impl.core.globalSettings();
    const auto stored = [settings](const char* key, const bool fallback) {
        return settings->select(QString::fromLatin1(key), fallback ? QStringLiteral("1") : QStringLiteral("0")) ==
               QStringLiteral("1");
    };
    const QScopedValueRollback<bool> restoring(impl.restoringConsole, true);
    impl.consoleDrawer->setFloating(
        stored(SettingsKeys::INTERFACE_CONSOLE_FLOATING, SettingsDefaults::INTERFACE_CONSOLE_FLOATING));
    if (stored(SettingsKeys::INTERFACE_CONSOLE_DRAWER_OPEN, SettingsDefaults::INTERFACE_CONSOLE_DRAWER_OPEN))
    {
        setSessionConsoleVisible(true);
    }
}

void ObservatoryWindow::placeChanged()
{
    Impl& impl = *m_impl;
    if (impl.shownTask != 0u && !isTaskPageShown())
    {
        leaveTaskPage();
    }
    // The readout is beside the workspace only: Home is no place to read an image, whether one is open or not.
    const bool column = isWorkspaceShown();
    if (impl.rightColumn->isVisibleTo(this) != column)
    {
        impl.rightColumn->setVisible(column);
    }
    // The layout icons are the workspace's.
    if (impl.layoutIcons != nullptr)
    {
        impl.layoutIcons->setVisible(isWorkspaceShown());
    }
    // A workspace left with no docked view closes, unless it is the only one.
    closeLeftEmptyWorkspaces();
    updateRail();
    updateBreadcrumb();
    updateTaskActions();
}

void ObservatoryWindow::activeViewChanged(ImageViewControl* view)
{
    Impl& impl = *m_impl;
    // Each workspace's chrome shows the active view of that workspace: the view goes to the page of the workspace it
    // is docked in; a view that floats has its own.
    if (view != nullptr)
    {
        if (WorkspacePage* const page = pageOf(*view))
        {
            page->bindView(view);
        }
    }
    else if (WorkspacePage* const page = workspacePage())
    {
        page->bindView(nullptr);
    }
    updateWorkspacePage();
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
            buildTabMenu(menu, *view);
            menu.exec(globalPos);
            return true;
        }
    }
    return false;
}

void ObservatoryWindow::dockIntoWorkspace(ImageViewControl& view, const QString& workspaceId)
{
    Impl& impl = *m_impl;
    KDDockWidgets::QtWidgets::DockWidget* const dock = view.getDockWidget();
    const QString workspace = workspaceId.isEmpty() ? impl.workspaces->current() : workspaceId;
    KDDockWidgets::QtWidgets::MainWindow* const area = impl.workspaces->mainWindow(workspace);
    if (dock == nullptr || area == nullptr)
    {
        return;
    }
    area->addDockWidget(dock, KDDockWidgets::Location_OnRight);
    // Docked again, its tools are the workspace's.
    QTimer::singleShot(0, this, [this, workspace]() {
        updateWorkspacePage();
        showWorkspace(workspace);
    });
}

void ObservatoryWindow::updateWorkspacePage()
{
    Impl& impl = *m_impl;
    std::vector<ImageViewControl*> views;
    std::vector<ImageViewControl*> floating;
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
    }
    impl.floatingChrome->update(views);
    ImageViewControl* const active = impl.core.viewService().getActiveImageViewControl();
    const bool only = impl.workspaces->model().count() == 1;
    for (const QString& id : impl.workspaces->ids())
    {
        WorkspacePage* const page = impl.workspaces->page(id);
        if (page == nullptr)
        {
            continue;
        }
        const std::vector<ImageViewControl*> docked = impl.workspaces->dockedViews(id, views);
        // A workspace with no docked view shows its quiet page: the only one lists the floating images, any other
        // says it is empty. With a docked one, the dock area is shown.
        const bool quiet = docked.empty();
        if (quiet)
        {
            page->floatingImages()->setViews(only ? floating : std::vector<ImageViewControl*>{});
        }
        page->showFloatingImages(quiet);
        page->binder().setViewCount(static_cast<int>(docked.size()));
        // Its chrome shows its active view: the application's when that is one of its own, else the one it shows if
        // that is still docked in it, else its first.
        const auto holds = [&docked](const ImageViewControl* view) {
            return view != nullptr && std::find(docked.begin(), docked.end(), view) != docked.end();
        };
        ImageViewControl* const bound = page->binder().toolbarView();
        ImageViewControl* const wanted =
            holds(active) ? active : (holds(bound) ? bound : (docked.empty() ? nullptr : docked.front()));
        if (wanted != bound)
        {
            page->bindView(wanted);
        }
    }
    refreshPendingChips();
}

void ObservatoryWindow::refreshPendingChips()
{
    Impl& impl = *m_impl;
    if (!impl.workspaces)
    {
        return;
    }
    for (const QString& id : impl.workspaces->ids())
    {
        WorkspacePage* const page = impl.workspaces->page(id);
        if (page == nullptr || page->historyStrip() == nullptr)
        {
            continue;
        }
        ImageViewControl* const shown = page->binder().historyView();
        std::optional<an::ui::HistoryPendingStep> pending =
            impl.tasks && shown != nullptr ? impl.tasks->pendingStepOf(shown) : std::nullopt;
        if (page->historyStrip()->pendingStep() != pending)
        {
            page->historyStrip()->setPendingStep(std::move(pending));
        }
    }
    // A task's page shows its target's history with the task's pending step.
    for (const auto& [id, page] : impl.taskPages)
    {
        if (page.isNull() || !impl.tasks)
        {
            continue;
        }
        ImageViewControl* const shown = page->binder().historyView();
        std::optional<an::ui::HistoryPendingStep> pending =
            shown != nullptr ? impl.tasks->pendingStepOf(shown) : std::nullopt;
        if (page->historyStrip()->pendingStep() != pending)
        {
            page->historyStrip()->setPendingStep(std::move(pending));
        }
    }
}

WorkspacePage* ObservatoryWindow::makeWorkspacePage(const QString& id, KDDockWidgets::QtWidgets::MainWindow& dockArea)
{
    FloatingImagesPage::Dependencies dependencies;
    dependencies.dockHere = [this, id](ImageViewControl& view) { dockIntoWorkspace(view, id); };
    dependencies.dockAll = [this, id]() {
        if (WorkspacePage* const page = m_impl->workspaces->page(id))
        {
            for (ImageViewControl* view : page->floatingImages()->views())
            {
                dockIntoWorkspace(*view, id);
            }
        }
    };
    dependencies.openFile = [this, id]() {
        // The file opens in the workspace whose page asked for it.
        m_impl->workspaces->setCurrent(id);
        openFile();
    };
    auto* const floating = new FloatingImagesPage(std::move(dependencies));
    auto* const page = new WorkspacePage(dockArea, *floating);
    page->setObjectName(QStringLiteral("observatoryWorkspacePage.") + id);
    page->binder().setMenuFiller(m_impl->viewMenuFiller);
    return page;
}

WorkspacePage* ObservatoryWindow::pageOf(ImageViewControl& view) const
{
    const QString workspace = m_impl->workspaces->workspaceOf(view);
    return workspace.isEmpty() ? nullptr : m_impl->workspaces->page(workspace);
}

void ObservatoryWindow::workspacesChanged(const an::ui::shell::WorkspaceModel::Change change, const QString& id)
{
    Impl& impl = *m_impl;
    if (!impl.workspaces)
    {
        return;
    }
    switch (change)
    {
    case an::ui::shell::WorkspaceModel::Change::Current:
        // What the window's units dock into, and what the layout's actions and icons offer, are the current
        // workspace's.
        impl.dockArea = impl.workspaces->mainWindow();
        offerTheLock(impl.workspaces->currentPage() != nullptr &&
                     impl.workspaces->currentPage()->maximizedView() != nullptr);
        break;
    case an::ui::shell::WorkspaceModel::Change::Renamed:
        if (WorkspaceWindow* const holder = workspaceWindow(id))
        {
            holder->setWindowTitle(impl.workspaces->workspaceName(id));
        }
        break;
    case an::ui::shell::WorkspaceModel::Change::Closed:
        impl.workspaceWindows.erase(id);
        break;
    default:
        break;
    }
    // A workspace made, closed or made current: what each page shows follows.
    if (change == an::ui::shell::WorkspaceModel::Change::Added ||
        change == an::ui::shell::WorkspaceModel::Change::Closed ||
        change == an::ui::shell::WorkspaceModel::Change::Current)
    {
        updateWorkspacePage();
    }
    updateWorkspaceActions();
    updateRail();
    updateBreadcrumb();
}

void ObservatoryWindow::closeLeftEmptyWorkspaces()
{
    Impl& impl = *m_impl;
    // A project's workspaces are empty while its images open: none of them is one the person left.
    if (!impl.workspaces || impl.closingWorkspaces || (impl.layout && impl.layout->isRestoring()))
    {
        return;
    }
    const QScopedValueRollback<bool> closing(impl.closingWorkspaces, true);
    an::ui::shell::WorkspaceModel& model = impl.workspaces->model();
    const QString onScreen = isWorkspaceShown() ? impl.workspaces->shown() : QString();
    const std::vector<ImageViewControl*> views = impl.core.viewService().getListOfViewControls();
    for (const QString& id : impl.workspaces->ids())
    {
        if (model.count() <= 1)
        {
            break;
        }
        const an::ui::shell::Workspace* const workspace = model.find(id);
        const WorkspacePage* const page = impl.workspaces->page(id);
        if (workspace == nullptr || workspace->detached || id == onScreen || page == nullptr ||
            page->maximizedView() != nullptr)
        {
            continue;
        }
        if (impl.workspaces->dockedViews(id, views).empty())
        {
            if (impl.core.dockLayout().isLocked(impl.workspaces->target(id)))
            {
                impl.core.dockLayout().unlock(impl.workspaces->target(id));
            }
            (void)impl.workspaces->close(id);
        }
    }
}

void ObservatoryWindow::showDockAreasForADrag()
{
    Impl& impl = *m_impl;
    // A workspace showing its quiet page shows its dock area while a view is dragged, so the drag can land there; what
    // the drag left is shown again once it ends.
    for (const QString& id : impl.workspaces->ids())
    {
        if (WorkspacePage* const page = impl.workspaces->page(id); page != nullptr && page->isFloatingImagesShown())
        {
            page->showFloatingImages(false);
        }
    }
}

void ObservatoryWindow::showWorkspace(const QString& id)
{
    Impl& impl = *m_impl;
    const an::ui::shell::Workspace* const workspace = impl.workspaces->model().find(id);
    if (workspace == nullptr)
    {
        showViews();
        return;
    }
    if (workspace->detached)
    {
        // A workspace in a window of its own is raised there.
        if (WorkspaceWindow* const holder = workspaceWindow(id))
        {
            holder->show();
            holder->raise();
            holder->activateWindow();
        }
        impl.workspaces->setCurrent(id);
        return;
    }
    impl.workspaces->setCurrent(id);
    showViews();
}

void ObservatoryWindow::newWorkspace()
{
    Impl& impl = *m_impl;
    const QString id = impl.workspaces->create();
    // A new workspace's layout is locked when the person keeps layouts locked by default.
    an::ui::IGlobalSettingsProvider* const settings = impl.core.globalSettings();
    if (settings != nullptr && settings->select(QString::fromLatin1(SettingsKeys::INTERFACE_LOCK_LAYOUT_BY_DEFAULT),
                                                SettingsDefaults::INTERFACE_LOCK_LAYOUT_BY_DEFAULT
                                                    ? QStringLiteral("1")
                                                    : QStringLiteral("0")) == QStringLiteral("1"))
    {
        lockWorkspace(id, true);
    }
    showWorkspace(id);
}

void ObservatoryWindow::renameWorkspace(const QString& id)
{
    Impl& impl = *m_impl;
    if (impl.workspaces->model().find(id) == nullptr)
    {
        return;
    }
    auto* const dialog = new RenameWorkspaceDialog(
        impl.workspaces->workspaceName(id),
        [this, id](const QString& name) { (void)m_impl->workspaces->model().rename(id, name); }, this);
    dialog->open();
}

void ObservatoryWindow::moveWorkspaceToNewWindow(const QString& id)
{
    Impl& impl = *m_impl;
    const an::ui::shell::Workspace* const workspace = impl.workspaces->model().find(id);
    if (workspace == nullptr || workspace->detached)
    {
        return;
    }
    const QSize size = impl.centre->size();
    WorkspacePage* const page = impl.workspaces->detach(id);
    if (page == nullptr)
    {
        return;
    }
    // The window's header holds the layout icons, acting on its workspace.
    WorkspaceToolbar::Dependencies icons;
    icons.actions = impl.core.actions().lookup();
    icons.isLocked = [this, id]() { return m_impl->core.dockLayout().isLocked(m_impl->workspaces->target(id)); };
    icons.setLocked = [this, id](const bool locked) { lockWorkspace(id, locked); };
    icons.icon = [this](const QString& name) { return m_impl->icon(name.toStdString()); };
    auto* const header = new WorkspaceToolbar(std::move(icons));
    header->setObjectName(QStringLiteral("workspaceWindowLayoutIcons"));
    WorkspaceWindow::Hooks hooks;
    hooks.activated = [this, id]() { m_impl->workspaces->setCurrent(id); };
    hooks.answered = [this, id](const WorkspaceWindow::Answer answer) {
        if (answer == WorkspaceWindow::Answer::Return)
        {
            returnWorkspace(id);
        }
        else
        {
            closeWorkspace(id);
        }
    };
    hooks.takesFiles = [](const QMimeData& files) { return an::ui::shell::FileDropHandler::takes(files); };
    hooks.filesDropped = [this, id](const QMimeData& files) {
        // Files dropped on a workspace open there.
        m_impl->workspaces->setCurrent(id);
        return m_impl->core.fileDrops().drop(files);
    };
    auto* const holder = new WorkspaceWindow(workspace->name, *header, *page, std::move(hooks));
    impl.workspaceWindows[id] = holder;
    holder->resize(size.isValid() && !size.isEmpty() ? size : QSize(kMinimumWidth, kMinimumHeight) / 2);
    holder->show();
    holder->activateWindow();
    impl.workspaces->setCurrent(id);
    // This window shows another workspace, or Home when every one is in a window of its own.
    if (isWorkspaceShown())
    {
        showViews();
    }
    updateWorkspaceActions();
    updateRail();
}

void ObservatoryWindow::returnWorkspace(const QString& id)
{
    Impl& impl = *m_impl;
    WorkspaceWindow* const holder = workspaceWindow(id);
    if (holder == nullptr)
    {
        return;
    }
    impl.workspaceWindows.erase(id);
    if (holder->takePage() != nullptr)
    {
        impl.workspaces->attach(id);
    }
    holder->closeWithoutAsking();
    holder->deleteLater();
    showWorkspace(id);
    updateWorkspacePage();
}

void ObservatoryWindow::closeWorkspace(const QString& id)
{
    Impl& impl = *m_impl;
    if (impl.workspaces->model().find(id) == nullptr)
    {
        return;
    }
    // Its images close first, each asked about as Close asks; one kept keeps the workspace.
    if (!closeViewsOf(id))
    {
        return;
    }
    if (workspaceWindow(id) != nullptr)
    {
        returnWorkspace(id);
    }
    if (impl.workspaces->model().canClose(id))
    {
        if (impl.core.dockLayout().isLocked(impl.workspaces->target(id)))
        {
            impl.core.dockLayout().unlock(impl.workspaces->target(id));
        }
        const bool wasShown = isWorkspaceShown() && impl.workspaces->shown() == id;
        (void)impl.workspaces->close(id);
        if (wasShown)
        {
            showViews();
        }
    }
    // With no image open, Home is the place.
    if (impl.core.viewService().getListOfViewControls().empty())
    {
        showHome();
    }
    updateWorkspacePage();
}

bool ObservatoryWindow::closeViewsOf(const QString& id)
{
    Impl& impl = *m_impl;
    QAction* const close = impl.core.actions().action(an::ui::shell::ids::kClose);
    if (close == nullptr)
    {
        return false;
    }
    const auto stillDocked = [this, &impl, &id]() {
        std::vector<ImageViewControl*> open;
        for (ImageViewControl* view : impl.workspaces->dockedViews(id, impl.core.viewService().getListOfViewControls()))
        {
            if (view != nullptr && view->getDockWidget() != nullptr && view->getDockWidget()->isOpen())
            {
                open.push_back(view);
            }
        }
        return open;
    };
    std::vector<QPointer<ImageViewControl>> closing;
    for (ImageViewControl* view : stillDocked())
    {
        closing.emplace_back(view);
    }
    for (const QPointer<ImageViewControl>& view : closing)
    {
        if (view.isNull())
        {
            continue;
        }
        // Each view is closed as Close closes the active one, asked about unsaved changes as it is.
        impl.core.viewService().setActiveImageViewControl(view.data());
        close->trigger();
    }
    return stillDocked().empty();
}

void ObservatoryWindow::lockWorkspace(const QString& id, const bool locked)
{
    Impl& impl = *m_impl;
    const an::ui::shell::DockTarget target = impl.workspaces->target(id);
    if (target.mainWindow == nullptr)
    {
        return;
    }
    if (locked)
    {
        impl.core.dockLayout().lock(target);
    }
    else
    {
        impl.core.dockLayout().unlock(target);
    }
    (void)impl.workspaces->model().setLocked(id, locked);
    // Lock and Unlock, and the icons, offer what the workspace on screen is not.
    impl.core.dockLayout().offerFor(dockTarget());
    if (impl.layoutIcons != nullptr)
    {
        impl.layoutIcons->refreshLock();
    }
    if (WorkspaceWindow* const holder = workspaceWindow(id); holder != nullptr && holder->header() != nullptr)
    {
        holder->header()->refreshLock();
    }
}

void ObservatoryWindow::moveViewToWorkspace(ImageViewControl& view, const QString& id)
{
    Impl& impl = *m_impl;
    KDDockWidgets::QtWidgets::MainWindow* const area = impl.workspaces->mainWindow(id);
    KDDockWidgets::QtWidgets::DockWidget* const dock = view.getDockWidget();
    if (area == nullptr || dock == nullptr || impl.workspaces->workspaceOf(view) == id)
    {
        return;
    }
    if (view.isMaximized())
    {
        view.switchMaximized();
    }
    area->addDockWidget(dock, KDDockWidgets::Location_OnRight);
    // The person follows the view to the workspace it went to; the one it left closes if it is empty.
    showWorkspace(id);
    impl.core.viewService().setActiveImageViewControl(&view);
    updateWorkspacePage();
    closeLeftEmptyWorkspaces();
    updateRail();
}

void ObservatoryWindow::updateWorkspaceActions()
{
    Impl& impl = *m_impl;
    if (!impl.shellActions || !impl.workspaces)
    {
        return;
    }
    const QString current = impl.workspaces->current();
    const an::ui::shell::Workspace* const workspace = impl.workspaces->model().find(current);
    if (QAction* const close = impl.shellActions->action(ids::kCloseWorkspace))
    {
        close->setEnabled(impl.workspaces->model().canClose(current));
    }
    if (QAction* const move = impl.shellActions->action(ids::kMoveWorkspaceToNewWindow))
    {
        move->setEnabled(workspace != nullptr && !workspace->detached);
    }
}

void ObservatoryWindow::buildWorkspaceMenu(QMenu& menu, const QString& workspaceId)
{
    Impl& impl = *m_impl;
    const an::ui::shell::Workspace* const workspace = impl.workspaces->model().find(workspaceId);
    if (workspace == nullptr)
    {
        return;
    }
    const QString id = workspaceId;
    const auto entry = [&menu](const char* name, const QString& text) {
        QAction* const action = menu.addAction(text);
        action->setObjectName(QString::fromLatin1(name));
        return action;
    };
    QObject::connect(entry("workspaceMenuNew", tr("New Workspace")), &QAction::triggered, this,
                     [this]() { newWorkspace(); });
    QObject::connect(entry("workspaceMenuRename", tr("Rename...")), &QAction::triggered, this,
                     [this, id]() { renameWorkspace(id); });
    QAction* const move = entry("workspaceMenuMoveToNewWindow", tr("Move to New Window"));
    move->setEnabled(!workspace->detached);
    QObject::connect(move, &QAction::triggered, this, [this, id]() { moveWorkspaceToNewWindow(id); });
    menu.addSeparator();
    QObject::connect(entry("workspaceMenuGroup", tr("Group All Images")), &QAction::triggered, this,
                     [this, id]() { m_impl->core.dockLayout().groupAll(m_impl->workspaces->target(id)); });
    QObject::connect(entry("workspaceMenuSplit", tr("Split All Images")), &QAction::triggered, this,
                     [this, id]() { m_impl->core.dockLayout().splitAllEqually(m_impl->workspaces->target(id)); });
    QObject::connect(entry("workspaceMenuFit", tr("Fit All Images to Window")), &QAction::triggered, this,
                     [this, id]() { m_impl->core.dockLayout().fitAllToWindow(m_impl->workspaces->target(id)); });
    menu.addSeparator();
    QAction* const lock = entry("workspaceMenuLock", tr("Lock Layout"));
    lock->setCheckable(true);
    lock->setChecked(impl.core.dockLayout().isLocked(impl.workspaces->target(id)));
    QObject::connect(lock, &QAction::triggered, this, [this, id](const bool locked) { lockWorkspace(id, locked); });
    menu.addSeparator();
    QAction* const close = entry("workspaceMenuClose", tr("Close Workspace"));
    close->setEnabled(impl.workspaces->model().canClose(id));
    QObject::connect(close, &QAction::triggered, this, [this, id]() { closeWorkspace(id); });
}

void ObservatoryWindow::buildTabMenu(QMenu& menu, ImageViewControl& view)
{
    Impl& impl = *m_impl;
    an::ui::shell::ViewMenuBuilder(impl.core).buildTabMenu(menu, view);
    // Move to Workspace, after Float: the other workspaces, a separator, a new one.
    QAction* after = nullptr;
    const QList<QAction*> entries = menu.actions();
    for (qsizetype index = 0; index < entries.size(); ++index)
    {
        if (entries[index]->text() == QObject::tr("Float"))
        {
            after = index + 1 < entries.size() ? entries[index + 1] : nullptr;
            break;
        }
    }
    auto* const move = new QMenu(tr("Move to Workspace"), &menu);
    move->setObjectName(QStringLiteral("menuMoveToWorkspace"));
    const QString from = impl.workspaces->workspaceOf(view);
    const QPointer<ImageViewControl> guarded(&view);
    for (const an::ui::shell::Workspace& workspace : impl.workspaces->model().workspaces())
    {
        if (workspace.id == from)
        {
            continue;
        }
        const QString id = workspace.id;
        QObject::connect(move->addAction(workspace.name), &QAction::triggered, this, [this, guarded, id]() {
            if (guarded)
            {
                moveViewToWorkspace(*guarded, id);
            }
        });
    }
    if (!move->isEmpty())
    {
        move->addSeparator();
    }
    QAction* const fresh = move->addAction(tr("New Workspace"));
    fresh->setObjectName(QStringLiteral("moveToNewWorkspace"));
    QObject::connect(fresh, &QAction::triggered, this, [this, guarded]() {
        if (guarded)
        {
            moveViewToWorkspace(*guarded, m_impl->workspaces->create());
        }
    });
    menu.insertMenu(after, move);
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

    // The workspace's, in the Window menu, acting on the current workspace.
    QObject::connect(actions.action(ids::kNewWorkspace), &QAction::triggered, this, [this]() { newWorkspace(); });
    QObject::connect(actions.action(ids::kRenameWorkspace), &QAction::triggered, this,
                     [this]() { renameWorkspace(m_impl->workspaces->current()); });
    QObject::connect(actions.action(ids::kMoveWorkspaceToNewWindow), &QAction::triggered, this,
                     [this]() { moveWorkspaceToNewWindow(m_impl->workspaces->current()); });
    QObject::connect(actions.action(ids::kCloseWorkspace), &QAction::triggered, this,
                     [this]() { closeWorkspace(m_impl->workspaces->current()); });
    if (QMenu* const window = impl.menuBar->menu("menuWindow"))
    {
        window->addSeparator();
        window->addAction(actions.action(ids::kNewWorkspace));
        window->addAction(actions.action(ids::kRenameWorkspace));
        window->addAction(actions.action(ids::kMoveWorkspaceToNewWindow));
        window->addAction(actions.action(ids::kCloseWorkspace));
    }
    updateWorkspaceActions();

    // The project's, in the File menu before Exit; what each does is the projects' unit's to connect.
    if (QMenu* const file = impl.menuBar->menu("menuFile"))
    {
        const QList<QAction*> entries = file->actions();
        const qsizetype exitAt = entries.indexOf(impl.core.actions().action(an::ui::shell::ids::kExit));
        QAction* before = exitAt >= 0 ? entries.at(exitAt) : nullptr;
        if (exitAt > 0 && entries.at(exitAt - 1)->isSeparator())
        {
            before = entries.at(exitAt - 1);
        }
        file->insertSeparator(before);
        for (const std::string_view id :
             {ids::kNewProject, ids::kOpenProject, ids::kSwitchProject, ids::kCloseProject, ids::kProjectSettings})
        {
            file->insertAction(before, actions.action(id));
        }
    }

    // Apply runs the current task's primary action: inside a page the page answers the key itself.
    QObject::connect(actions.action(ids::kApplyTask), &QAction::triggered, this, [this]() { applyCurrentTask(); });
    if (QMenu* const process = impl.menuBar->menu("menuProcess"))
    {
        process->addSeparator();
        process->addAction(actions.action(ids::kApplyTask));
    }

    // The current task's place, in the Window menu: the page shown floats; the task whose window is active attaches.
    QObject::connect(actions.action(ids::kFloatTask), &QAction::triggered, this, [this]() {
        if (isTaskPageShown())
        {
            (void)floatTask(m_impl->shownTask);
        }
    });
    QObject::connect(actions.action(ids::kAttachTask), &QAction::triggered, this, [this]() {
        if (m_impl->tasks)
        {
            (void)attachTask(m_impl->tasks->model().current());
        }
    });
    QObject::connect(actions.action(ids::kCloseTask), &QAction::triggered, this, [this]() {
        if (m_impl->tasks)
        {
            (void)m_impl->tasks->close(m_impl->tasks->model().current());
        }
    });
    if (QMenu* const window = impl.menuBar->menu("menuWindow"))
    {
        window->addSeparator();
        window->addAction(actions.action(ids::kFloatTask));
        window->addAction(actions.action(ids::kAttachTask));
        window->addAction(actions.action(ids::kCloseTask));
    }
    updateTaskActions();

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
    if (impl.topBar == nullptr || !impl.workspaces)
    {
        return;
    }
    QStringList parts;
    const an::ui::shell::Task* const task =
        isTaskPageShown() && impl.tasks ? impl.tasks->model().task(impl.shownTask) : nullptr;
    if (task != nullptr)
    {
        // A task's page: the process and the image it works on.
        parts.append(task->displayName);
        if (!task->targetName.isEmpty())
        {
            parts.append(task->targetName);
        }
    }
    else if (isWorkspaceShown())
    {
        const QString shown = impl.workspaces->shown();
        parts.append(impl.workspaces->workspaceName(shown));
        // The workspace's item is its active view; one that floats, or is in another workspace, is not its item.
        if (ImageViewControl* const active = impl.core.viewService().getActiveImageViewControl();
            active != nullptr && impl.workspaces->workspaceOf(*active) == shown)
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
    lockWorkspace(m_impl->workspaces->current(), locked);
}

void ObservatoryWindow::updateRail()
{
    Impl& impl = *m_impl;
    if (impl.rail == nullptr || !impl.workspaces)
    {
        return;
    }
    const an::ui::shell::WorkspaceModel& model = impl.workspaces->model();
    const bool several = model.count() > 1;
    const QString selectedWorkspace = isWorkspaceShown() ? impl.workspaces->shown() : QString();
    const auto item = [](const QString& id, const an::ui::RailSection section, const QString& iconKey,
                         const QString& text) {
        an::ui::RailItem made;
        made.id = id;
        made.section = section;
        made.iconKey = iconKey;
        made.text = text;
        return made;
    };
    std::vector<an::ui::RailItem> items;
    an::ui::RailItem home =
        item(QString::fromLatin1(kRailHome), an::ui::RailSection::Place, QStringLiteral("home"), tr("Home"));
    home.selected = isHomeShown();
    items.push_back(home);
    // A workspace each, a digit on each when there are several; one in a window of its own carries the window glyph.
    int number = 0;
    for (const an::ui::shell::Workspace& workspace : model.workspaces())
    {
        ++number;
        an::ui::RailItem place =
            item(railWorkspaceId(workspace.id), an::ui::RailSection::Place, QStringLiteral("images"), workspace.name);
        place.badgeText = several && !workspace.detached ? QString::number(number) : QString();
        place.floating = workspace.detached;
        place.selected = workspace.id == selectedWorkspace;
        items.push_back(place);
    }
    items.push_back(item(QString::fromLatin1(kRailIntegration), an::ui::RailSection::Place, QStringLiteral("layers"),
                         tr("Integration")));
    appendTaskItems(items);
    an::ui::RailItem console = item(QString::fromLatin1(kRailConsole), an::ui::RailSection::Foot,
                                    QStringLiteral("terminal"), tr("Session Console"));
    const int unseen = impl.consoleDrawer != nullptr ? impl.consoleDrawer->unseenWarnings() : 0;
    console.badgeText = unseen > 0 ? QString::number(unseen) : QString();
    items.push_back(console);
    items.push_back(item(QString::fromLatin1(kRailSettings), an::ui::RailSection::Foot, QStringLiteral("settings"),
                         tr("Settings")));

    // The same items are updated where they differ; another set of them is set whole.
    const std::vector<an::ui::RailItem>& shown = impl.rail->items();
    bool sameIds = shown.size() == items.size();
    for (std::size_t index = 0; sameIds && index < items.size(); ++index)
    {
        sameIds = shown[index].id == items[index].id;
    }
    if (!sameIds)
    {
        impl.rail->setItems(items);
        return;
    }
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        if (!an::ui::sameRailItem(shown[index], items[index]))
        {
            impl.rail->updateItem(items[index]);
        }
    }
}

void ObservatoryWindow::appendTaskItems(std::vector<an::ui::RailItem>& items) const
{
    const Impl& impl = *m_impl;
    if (!impl.tasks)
    {
        return;
    }
    const an::ui::shell::TaskModel& model = impl.tasks->model();
    for (const an::ui::shell::Task& task : model.tasks())
    {
        an::ui::RailItem made;
        made.id = railTaskId(task.id);
        made.section = an::ui::RailSection::Task;
        made.text = task.targetName.isEmpty() ? task.displayName
                                              : QStringLiteral("%1 - %2").arg(task.displayName, task.targetName);
        made.thumbnail = task.target.isNull() ? QImage() : task.thumbnail;
        made.glyphKey = task.iconKey;
        made.floating = task.presentation == an::ui::shell::TaskPresentation::Window;
        made.selected = task.id == model.current() && task.presentation == an::ui::shell::TaskPresentation::Page;
        if (task.run.running())
        {
            made.runPhase = an::ui::RailRunPhase::Running;
            if (task.run.progress.has_value())
            {
                made.progress = static_cast<int>(std::lround(*task.run.progress * 100.0));
            }
        }
        else if (task.unseenResult)
        {
            made.runPhase = task.run.phase == an::ui::ProcessRunState::Phase::Failed ? an::ui::RailRunPhase::Failed
                                                                                     : an::ui::RailRunPhase::Succeeded;
            made.unseenResult = true;
        }
        items.push_back(std::move(made));
    }
}

void ObservatoryWindow::buildTaskMenu(QMenu& menu, const an::ui::shell::TaskId id)
{
    TaskPresenter* const tasks = m_impl->tasks.get();
    const an::ui::shell::Task* task = tasks != nullptr ? tasks->model().task(id) : nullptr;
    if (task == nullptr)
    {
        return;
    }
    QAction* const show = menu.addAction(tr("Show"));
    show->setObjectName(QStringLiteral("actionShowTask"));
    QObject::connect(show, &QAction::triggered, this, [this, id]() {
        if (m_impl->tasks)
        {
            (void)m_impl->tasks->show(id);
        }
    });
    // A page floats in a window of its own and attaches again; a process shown as a window of its own has no page.
    const bool floats = task->presentation == an::ui::shell::TaskPresentation::Window;
    QAction* const place = menu.addAction(floats ? tr("Attach") : tr("Float as Window"));
    place->setObjectName(QStringLiteral("actionPlaceTask"));
    place->setEnabled(taskPage(id) != nullptr);
    QObject::connect(place, &QAction::triggered, this,
                     [this, id, floats]() { (void)(floats ? attachTask(id) : floatTask(id)); });
    menu.addSeparator();
    QAction* const close = menu.addAction(tr("Close"));
    close->setObjectName(QStringLiteral("actionCloseTask"));
    close->setEnabled(tasks->canClose(id));
    QObject::connect(close, &QAction::triggered, this, [this, id]() {
        if (m_impl->tasks)
        {
            (void)m_impl->tasks->close(id);
        }
    });
}

void ObservatoryWindow::railActivated(const QString& id)
{
    Impl& impl = *m_impl;
    const QString workspacePrefix = railWorkspaceId(QString());
    const QString taskPrefix = railTaskId(0u).chopped(1);
    if (id.startsWith(taskPrefix))
    {
        bool numbered = false;
        const an::ui::shell::TaskId task = id.mid(taskPrefix.size()).toULongLong(&numbered);
        if (numbered && impl.tasks)
        {
            (void)impl.tasks->show(task);
        }
        return;
    }
    if (id == QLatin1String(kRailHome))
    {
        showHome();
    }
    else if (id.startsWith(workspacePrefix))
    {
        // With no image open and one workspace, Home is the place for it.
        if (impl.core.viewService().getListOfViewControls().empty() && impl.workspaces->model().count() == 1)
        {
            return;
        }
        showWorkspace(id.mid(workspacePrefix.size()));
    }
    else if (id == QLatin1String(kRailIntegration))
    {
        openImageIntegration();
    }
    else if (id == QLatin1String(kRailConsole))
    {
        setSessionConsoleVisible(!isSessionConsoleVisible());
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

TaskPage* ObservatoryWindow::taskPage(const an::ui::shell::TaskId id) const
{
    const auto found = m_impl->taskPages.find(id);
    return found != m_impl->taskPages.end() ? found->second.data() : nullptr;
}

bool ObservatoryWindow::isTaskPageShown() const
{
    const auto* const page = dynamic_cast<const TaskPage*>(m_impl->centre->currentWidget());
    return page != nullptr;
}

an::ui::shell::TaskId ObservatoryWindow::shownTask() const noexcept
{
    return m_impl->shownTask;
}

an::ui::AnSnackbar* ObservatoryWindow::snackbar() const noexcept
{
    return m_impl->snackbar;
}

an::ui::shell::ReadoutController& ObservatoryWindow::readoutController() const noexcept
{
    return *m_impl->readout;
}

bool ObservatoryWindow::adoptTaskPage(TopLevelProcessBaseControl& process)
{
    Impl& impl = *m_impl;
    const an::ui::shell::Task* const task = impl.tasks ? impl.tasks->model().taskFor(&process) : nullptr;
    if (task == nullptr)
    {
        return false;
    }
    const an::ui::shell::TaskId id = task->id;
    auto* const page = new TaskPage(impl.centre);
    page->setObjectName(QStringLiteral("observatoryTaskPage.%1").arg(id));
    if (!process.attachToPage(*page->processHost()))
    {
        delete page;
        return false;
    }
    impl.centre->addWidget(page);
    page->dress(process, [this, id](ImageViewControl* preview) {
        // The canvas the looking actions act on is the preview shown now.
        if (m_impl->shownTask == id)
        {
            const an::ui::shell::Task* const shown = m_impl->tasks ? m_impl->tasks->model().task(id) : nullptr;
            m_impl->core.viewService().setTaskSubject(shown != nullptr ? shown->target.data() : nullptr, preview);
            m_impl->core.actionState().updateActiveImageActions();
        }
    });
    page->binder().setMenuFiller(impl.viewMenuFiller);
    if (an::ui::ProcessPageLayout* const layout = process.pageLayout())
    {
        const std::string_view menu = an::ui::shell::ProcessMenuModel::menuOfCategory(task->category.toStdString());
        for (const an::ui::shell::ProcessMenuModel::Category& category : an::ui::shell::ProcessMenuModel::categories())
        {
            if (category.menuId == menu)
            {
                layout->setCategory(category.title);
            }
        }
    }
    an::ui::AnTaskHeader* const header = page->header();
    // Float as window, or Attach in a floating task.
    QObject::connect(header, &an::ui::AnTaskHeader::floatRequested, this,
                     [this, id]() { (void)(taskWindow(id) != nullptr ? attachTask(id) : floatTask(id)); });
    QPointer<TopLevelProcessBaseControl> guarded(&process);
    QObject::connect(header, &an::ui::AnTaskHeader::closeRequested, this, [guarded]() {
        if (guarded && !guarded->runState().running())
        {
            (void)guarded->close();
        }
    });
    // A full page's own menus, under the header's button.
    QPointer<TaskPage> shown(page);
    QObject::connect(header, &an::ui::AnTaskHeader::menuRequested, this, [shown]() {
        if (shown.isNull())
        {
            return;
        }
        QMenu* const menu = shown->makeProcessMenu(shown.data());
        menu->setAttribute(Qt::WA_DeleteOnClose);
        const QWidget* const button = shown->header()->menuButton();
        menu->popup(button->mapToGlobal(QPoint(0, button->height())));
    });
    impl.taskPages[id] = page;
    refreshTaskHeader(id);
    return true;
}

void ObservatoryWindow::showTaskPage(const an::ui::shell::TaskId id)
{
    Impl& impl = *m_impl;
    TaskPage* const page = taskPage(id);
    const an::ui::shell::Task* const task = impl.tasks ? impl.tasks->model().task(id) : nullptr;
    if (page == nullptr || task == nullptr)
    {
        return;
    }
    if (!isTaskPageShown())
    {
        impl.placeBeforeTask = impl.centre->currentWidget();
    }
    else if (impl.shownTask != id)
    {
        leaveTaskPage();
    }
    impl.centre->setCurrentWidget(page);
    impl.shownTask = id;
    ImageViewControl* const target = task->target.data();
    auto* const preview = dynamic_cast<ImageRTPreviewControl*>(task->process.data());
    // The canvas is the page's view: the preview, or a mirror page's view of the target.
    const an::ui::ProcessPageLayout* const layout = task->process ? task->process->pageLayout() : nullptr;
    ImageViewControl* const canvas = layout != nullptr ? layout->previewView() : nullptr;
    // The image actions act on the target, the looking ones on the canvas.
    if (target != nullptr && impl.core.viewService().getActiveImageViewControl() != target)
    {
        impl.core.viewService().setActiveImageViewControl(target);
    }
    impl.core.viewService().setTaskSubject(target, canvas);
    page->bindViews(canvas, target);
    // The compact readout reads the task's preview.
    if (impl.readout)
    {
        impl.readout->followTask(nullptr);
        impl.readoutSource.reset();
        if (preview != nullptr && task->process->processSurface() == an::ui::ProcessSurface::PreviewPage)
        {
            impl.readoutSource = std::make_unique<TaskReadoutSource>(*preview);
            impl.readout->setCompactReadout(page->compactReadout());
            impl.readout->followTask(impl.readoutSource.get());
        }
    }
    impl.core.actionState().updateActiveImageActions();
    impl.appliedTicker.start();
    refreshTaskHeader(id);
    updateTaskActions();
    refreshPendingChips();
    placeChanged();
    if (canvas != nullptr)
    {
        impl.statusLine->showImageInfo(canvas);
        impl.statusLine->showZoom(canvas);
    }
}

void ObservatoryWindow::leaveTaskPage()
{
    Impl& impl = *m_impl;
    if (impl.shownTask == 0u)
    {
        return;
    }
    impl.shownTask = 0u;
    if (impl.taskWindows.empty())
    {
        impl.appliedTicker.stop();
    }
    impl.core.viewService().clearTaskSubject();
    if (impl.readout)
    {
        impl.readout->followTask(nullptr);
        impl.readout->setCompactReadout(nullptr);
    }
    impl.readoutSource.reset();
    if (impl.tasks)
    {
        const an::ui::shell::Task* const current = impl.tasks->model().task(impl.tasks->model().current());
        if (current != nullptr && current->presentation == an::ui::shell::TaskPresentation::Page)
        {
            (void)impl.tasks->model().setCurrent(0u);
        }
    }
    impl.core.actionState().updateActiveImageActions();
    if (ImageViewControl* const active = impl.core.viewService().getActiveImageViewControl())
    {
        impl.statusLine->showImageInfo(active);
        impl.statusLine->showZoom(active);
    }
}

void ObservatoryWindow::taskRunChanged(const an::ui::shell::TaskId id, const an::ui::ProcessRunState& before)
{
    Impl& impl = *m_impl;
    const an::ui::shell::Task* const task = impl.tasks ? impl.tasks->model().task(id) : nullptr;
    if (task == nullptr || task->process.isNull())
    {
        return;
    }
    const an::ui::ProcessRunState now = task->process->runState();
    const auto at = std::chrono::steady_clock::now();
    // The end of a run is said to assistive technology: applied, politely; failed, with why, at once; stopped.
    if (before.running() && now.ended())
    {
        QString message;
        auto politeness = QAccessible::AnnouncementPoliteness::Polite;
        if (now.phase == an::ui::ProcessRunState::Phase::Succeeded)
        {
            message = task->targetName.isEmpty() ? tr("%1 applied").arg(task->displayName)
                                                 : tr("%1 applied to %2").arg(task->displayName, task->targetName);
        }
        else if (now.phase == an::ui::ProcessRunState::Phase::Failed)
        {
            message = now.text.isEmpty() ? tr("%1 failed").arg(task->displayName)
                                         : tr("%1 failed: %2").arg(task->displayName, now.text);
            politeness = QAccessible::AnnouncementPoliteness::Assertive;
        }
        else
        {
            message = tr("%1 stopped").arg(task->displayName);
        }
        QObject* const teller = taskPage(id) != nullptr ? static_cast<QObject*>(taskPage(id)) : this;
        QAccessibleAnnouncementEvent announcement(teller, message);
        announcement.setPoliteness(politeness);
        QAccessible::updateAccessibility(&announcement);
    }
    if (now.running() && !before.running())
    {
        impl.runStarted[id] = at;
        impl.appliedAt.erase(id);
    }
    // A page's run that succeeded is told in its header for a minute, floating or not, and in the snackbar of the
    // window it is in.
    if (now.phase == an::ui::ProcessRunState::Phase::Succeeded && before.running() && taskPage(id) != nullptr)
    {
        impl.appliedAt[id] = at;
    }
    if (now.phase == an::ui::ProcessRunState::Phase::Succeeded && before.running() &&
        task->presentation == an::ui::shell::TaskPresentation::Page)
    {
        const auto started = impl.runStarted.find(id);
        const double seconds =
            started != impl.runStarted.end() ? std::chrono::duration<double>(at - started->second).count() : 0.0;
        QPointer<ImageViewControl> target(task->target.data());
        showSnackbar(tr("%1 applied").arg(task->displayName), QStringLiteral("%1 s").arg(seconds, 0, 'f', 1),
                     tr("Undo"), [this, target]() {
                         if (target.isNull())
                         {
                             return;
                         }
                         m_impl->core.viewService().setActiveImageViewControl(target.data());
                         if (QAction* const undo = m_impl->core.actions().action(an::ui::shell::ids::kUndo))
                         {
                             undo->trigger();
                         }
                     });
    }
    refreshTaskHeader(id);
    updateTaskActions();
}

void ObservatoryWindow::refreshTaskPages()
{
    Impl& impl = *m_impl;
    if (!impl.tasks)
    {
        return;
    }
    for (auto entry = impl.taskPages.begin(); entry != impl.taskPages.end();)
    {
        const an::ui::shell::TaskId id = entry->first;
        if (impl.tasks->model().task(id) != nullptr && !entry->second.isNull())
        {
            refreshTaskHeader(id);
            ++entry;
            continue;
        }
        QPointer<TaskPage> page = entry->second;
        entry = impl.taskPages.erase(entry);
        impl.runStarted.erase(id);
        impl.appliedAt.erase(id);
        // A floating task's window goes once its process has: the readout first, the size kept as its process's.
        if (const auto floating = impl.taskWindows.find(id); floating != impl.taskWindows.end())
        {
            stopFloatingReadout(id);
            rememberFloatingSize(id);
            TaskWindow* const gone = floating->second.release();
            impl.taskWindows.erase(floating);
            impl.floatingProcessNames.erase(id);
            gone->deleteLater();
        }
        const bool shown = !page.isNull() && impl.centre->currentWidget() == page.data();
        if (shown || impl.shownTask == id)
        {
            leaveTaskPage();
            showPlaceBeforeTask();
        }
        if (!page.isNull())
        {
            impl.centre->removeWidget(page.data());
            page->hide();
            page->deleteLater();
        }
    }
}

void ObservatoryWindow::refreshTaskHeader(const an::ui::shell::TaskId id)
{
    Impl& impl = *m_impl;
    TaskPage* const page = taskPage(id);
    const an::ui::shell::Task* const task = impl.tasks ? impl.tasks->model().task(id) : nullptr;
    if (page == nullptr || task == nullptr || task->process.isNull())
    {
        return;
    }
    an::ui::AnTaskHeader* const header = page->header();
    header->setThumbnail(task->thumbnail);
    header->setProcessName(task->displayName);
    header->setTargetName(task->targetName);
    header->setFloating(task->presentation == an::ui::shell::TaskPresentation::Window);
    if (TaskWindow* const floating = taskWindow(id))
    {
        floating->setTitle(task->targetName.isEmpty()
                               ? task->displayName
                               : QStringLiteral("%1 - %2").arg(task->displayName, task->targetName));
    }
    const an::ui::ProcessRunState& run = task->process->runState();
    header->closeButton()->setEnabled(!run.running());
    if (run.running())
    {
        QString text = run.text.isEmpty() ? an::ui::applyingRunText() : run.text;
        if (run.progress.has_value())
        {
            text = tr("%1 - %2 %").arg(text).arg(static_cast<int>(std::lround(*run.progress * 100.0)));
        }
        header->setState(an::ui::AnTaskHeader::State::Running, text, run.progress);
        return;
    }
    if (run.phase == an::ui::ProcessRunState::Phase::Failed)
    {
        header->setState(an::ui::AnTaskHeader::State::Failed, tr("failed"));
        return;
    }
    // A run that succeeded is told for a minute after it ended; the preview's state otherwise.
    if (const auto applied = impl.appliedAt.find(id); applied != impl.appliedAt.end())
    {
        const auto since = std::chrono::steady_clock::now() - applied->second;
        const double seconds = std::chrono::duration<double>(since).count();
        if (seconds < 60.0)
        {
            header->setState(an::ui::AnTaskHeader::State::Applied,
                             tr("applied %1 s ago").arg(seconds, 0, 'f', seconds < 10.0 ? 1 : 0));
            return;
        }
    }
    const auto* const preview = dynamic_cast<const ImageRTPreviewControl*>(task->process.data());
    if (preview != nullptr && preview->previewView() != nullptr)
    {
        header->setState(an::ui::AnTaskHeader::State::UpToDate, tr("preview up to date"));
    }
    else
    {
        header->setState(an::ui::AnTaskHeader::State::None, QString());
    }
}

void ObservatoryWindow::showSnackbar(const QString& text, const QString& detail, const QString& action,
                                     std::function<void()> act)
{
    Impl& impl = *m_impl;
    impl.snackbarAction = std::move(act);
    impl.snackbar->setText(text);
    impl.snackbar->setDetail(detail);
    impl.snackbar->setActionText(action);
    impl.snackbar->present();
    placeSnackbar();
    impl.snackbar->raise();
}

void ObservatoryWindow::placeSnackbar()
{
    Impl& impl = *m_impl;
    if (impl.snackbar == nullptr || impl.centre == nullptr)
    {
        return;
    }
    // At the foot of the centre, in its middle, a little above the status line.
    const QSize size = impl.snackbar->sizeHint();
    const QRect centre(impl.centre->mapTo(centralWidget(), QPoint(0, 0)), impl.centre->size());
    const int statusTop = impl.statusLine->mapTo(centralWidget(), QPoint(0, 0)).y();
    const int bottom = (std::min)(centre.bottom() + 1, statusTop) - an::ui::parts::kSnackbarAboveStatusLine;
    impl.snackbar->setGeometry(centre.center().x() - size.width() / 2, bottom - size.height(), size.width(),
                               size.height());
}

void ObservatoryWindow::applyCurrentTask()
{
    Impl& impl = *m_impl;
    const an::ui::shell::Task* const task =
        impl.tasks ? impl.tasks->model().task(impl.tasks->model().current()) : nullptr;
    if (task != nullptr && !task->process.isNull())
    {
        (void)task->process->triggerPrimaryAction();
    }
}

// ---- A project's workspaces

bool ObservatoryWindow::leaveProject()
{
    Impl& impl = *m_impl;
    if (!impl.layout)
    {
        return true;
    }
    if (impl.layout->isRestoring())
    {
        // An arrangement half restored is not one to keep, and the images still opening would arrive with no project.
        showSnackbar(tr("The project's images are still opening"), tr("It can be closed once they are open"), QString(),
                     {});
        return false;
    }
    (void)impl.layout->save();
    // Its images close with it, each asked about as Close asks; one the person keeps keeps the project.
    std::vector<QPointer<ImageViewControl>> closing;
    for (ImageViewControl* const view : impl.layout->projectViews())
    {
        closing.emplace_back(view);
    }
    for (const QPointer<ImageViewControl>& view : closing)
    {
        if (view.isNull() || !view->checkPendingChangesAndAskUserBeforeClosing() ||
            !view->checkProcessAttachedAndAskUserBeforeClosing())
        {
            continue;
        }
        view->deepClose();
    }
    const bool left = impl.layout->projectViews().empty();
    updateWorkspacePage();
    if (impl.core.viewService().getListOfViewControls().empty())
    {
        showHome();
    }
    return left;
}

void ObservatoryWindow::takeProjectWorkspaces(const std::vector<an::ui::shell::ProjectWorkspace>& listed)
{
    Impl& impl = *m_impl;
    if (listed.empty())
    {
        return;
    }
    WorkspaceHost& host = *impl.workspaces;
    an::ui::shell::WorkspaceModel& model = host.model();
    // No workspace is closed for being empty while the project's are made.
    const QScopedValueRollback<bool> closing(impl.closingWorkspaces, true);
    // A workspace in a window of its own comes back, and a view shown over its whole page goes back to its dock.
    for (const QString& id : host.ids())
    {
        if (workspaceWindow(id) != nullptr)
        {
            returnWorkspace(id);
        }
        if (WorkspacePage* const page = host.page(id); page != nullptr && page->maximizedView() != nullptr)
        {
            page->maximizedView()->switchMaximized();
        }
    }
    const auto isListed = [&listed](const QString& id) {
        return std::any_of(listed.begin(), listed.end(),
                           [&id](const an::ui::shell::ProjectWorkspace& workspace) { return workspace.id == id; });
    };
    // The project's workspaces, each under the id its dock area is named by.
    for (const an::ui::shell::ProjectWorkspace& workspace : listed)
    {
        if (model.find(workspace.id) != nullptr)
        {
            (void)model.rename(workspace.id, workspace.name);
        }
        else
        {
            (void)host.createWithId(workspace.id, workspace.name);
        }
    }
    // A workspace the project does not list goes. A view docked in it belongs to none of the project's, and floats.
    const std::vector<ImageViewControl*> views = impl.core.viewService().getListOfViewControls();
    for (const QString& id : host.ids())
    {
        if (isListed(id))
        {
            continue;
        }
        for (ImageViewControl* const view : host.dockedViews(id, views))
        {
            if (view != nullptr && view->getDockWidget() != nullptr)
            {
                view->getDockWidget()->setFloating(true);
            }
        }
        if (impl.core.dockLayout().isLocked(host.target(id)))
        {
            impl.core.dockLayout().unlock(host.target(id));
        }
        if (model.current() == id)
        {
            host.setCurrent(listed.front().id);
        }
        (void)host.close(id);
    }
    // In the project's order, and free: the arrangement is put back into dock areas no lock holds, and the locks
    // follow it.
    int index = 0;
    for (const an::ui::shell::ProjectWorkspace& workspace : listed)
    {
        (void)model.move(workspace.id, index++);
        lockWorkspace(workspace.id, false);
    }
    updateWorkspacePage();
    updateRail();
}

void ObservatoryWindow::openProjectImages(const QStringList& files, std::function<void()> done)
{
    Impl& impl = *m_impl;
    an::ui::FileWorkflowRunner::Dependencies open;
    open.context = this;
    open.dialogParent = this;
    open.globalSettingsPersistence = impl.core.globalSettingsStore();
    open.rawOpenPolicy = [this]() { return m_impl->core.rawOpenPolicy(); };
    // The views are the project's to place, not the person's just opened: the place on screen stays where it is until
    // the arrangement is back, and the folder last opened and the recent files stay the person's.
    open.viewFactory = &impl.core.viewService();
    open.updateImageViewControlList = [this]() { refreshViewSelector(); };
    open.openFinished = [done = std::move(done)](const an::ui::FileOpenOutcome& /*outcome*/) {
        if (done)
        {
            done();
        }
    };
    auto* const progress = new ProgressDialogControl(this, "Open Project");
    progress->setProgressBarRange(0, files.size() >= 3 ? 100 : 0);
    progress->show();
    an::ui::FileWorkflowRunner::openFiles(open, files, QString(), progress);
}

void ObservatoryWindow::dockRestoredView(ImageViewControl& view, const QString& workspaceId)
{
    Impl& impl = *m_impl;
    WorkspaceHost& host = *impl.workspaces;
    KDDockWidgets::QtWidgets::DockWidget* const dock = view.getDockWidget();
    QString workspace = workspaceId;
    if (workspace.isEmpty() || host.model().find(workspace) == nullptr)
    {
        workspace = host.shown().isEmpty() ? host.current() : host.shown();
    }
    KDDockWidgets::QtWidgets::MainWindow* const area = host.mainWindow(workspace);
    if (dock == nullptr || area == nullptr)
    {
        return;
    }
    // As the files a person opens together are: the first at the right of what the workspace holds, the others as
    // tabs beside it.
    const std::vector<ImageViewControl*> docked =
        host.dockedViews(workspace, impl.core.viewService().getListOfViewControls());
    ImageViewControl* const beside = docked.empty() ? nullptr : docked.back();
    if (beside != nullptr && beside != &view && beside->getDockWidget() != nullptr)
    {
        beside->getDockWidget()->addDockWidgetAsTab(dock);
    }
    else
    {
        area->addDockWidget(dock, KDDockWidgets::Location_OnRight);
    }
}

void ObservatoryWindow::projectLayoutRestored(const an::ui::shell::ProjectLayoutRestored& restored)
{
    Impl& impl = *m_impl;
    // Each image is fitted to the place the arrangement gave its view, as a new view's is once it is docked.
    for (const QPointer<ImageViewControl>& view : restored.views)
    {
        if (view.isNull())
        {
            continue;
        }
        QTimer::singleShot(0, view.data(), [view]() {
            if (!view.isNull())
            {
                view->adjustImageToWindow();
            }
        });
    }
    if (restored.projectStillOpen)
    {
        // The workspace used last is the current one again, and is shown once there is an image to show in it.
        const QString last = impl.workspaces->model().find(restored.lastWorkspace) != nullptr
                                 ? restored.lastWorkspace
                                 : impl.workspaces->current();
        impl.workspaces->setCurrent(last);
        if (!impl.core.viewService().getListOfViewControls().empty() && !isTaskPageShown())
        {
            showWorkspace(last);
        }
        // The view the person looks at is one of that workspace's: the tab shown of its first group.
        for (ImageViewControl* const view :
             impl.workspaces->dockedViews(last, impl.core.viewService().getListOfViewControls()))
        {
            KDDockWidgets::QtWidgets::DockWidget* const dock = view != nullptr ? view->getDockWidget() : nullptr;
            if (dock != nullptr && dock->dockWidget()->isCurrentTab())
            {
                impl.core.viewService().setActiveImageViewControl(view);
                break;
            }
        }
    }
    updateWorkspacePage();
    refreshViewSelector();
    updateRail();
    updateBreadcrumb();
    if (restored.projectStillOpen && impl.projects)
    {
        impl.projects->reportMissingImages(restored.missing);
    }
}

void ObservatoryWindow::openProjectImage(const QString& file, const QString& imageId)
{
    Impl& impl = *m_impl;
    // In the workspace the project lists it in, while there is one; the workspace on screen otherwise.
    const QString workspace = impl.layout ? impl.layout->workspaceOf(imageId) : QString();
    if (!workspace.isEmpty() && impl.workspaces->model().find(workspace) != nullptr)
    {
        impl.workspaces->setCurrent(workspace);
    }
    if (impl.fileManager)
    {
        impl.fileManager->openFiles({file}, QFileInfo(file).absolutePath());
    }
}

void ObservatoryWindow::arrangementChanged()
{
    updateWorkspacePage();
    updateBreadcrumb();
    // A view floated out of the dock area or docked back: with none left the place of the views stays, as long as one
    // is open; Home is shown once none is.
    if (m_impl->core.viewService().getListOfViewControls().empty())
    {
        showHome();
    }
}

// ---- Floating tasks

TaskWindow* ObservatoryWindow::taskWindow(const an::ui::shell::TaskId id) const
{
    const auto found = m_impl->taskWindows.find(id);
    return found != m_impl->taskWindows.end() ? found->second.get() : nullptr;
}

TaskDrag& ObservatoryWindow::taskDrag() const noexcept
{
    return *m_impl->taskDrag;
}

QWidget* ObservatoryWindow::attachZoneOverlay() const noexcept
{
    return m_impl->attachZone;
}

void ObservatoryWindow::setDragPlatform(const QString& platformName)
{
    m_impl->dragPlatform = platformName;
}

bool ObservatoryWindow::taskWindowDragEvent(QObject* watched, QEvent* event)
{
    Impl& impl = *m_impl;
    if (!impl.taskDrag || impl.taskDrag->phase() != TaskDrag::Phase::Window ||
        impl.dragSurface->windowFollowsTheCursor())
    {
        return false;
    }
    const auto* const widget = qobject_cast<const QWidget*>(watched);
    if (widget == nullptr || widget->window() != this)
    {
        return false;
    }
    if (event->type() == QEvent::DragLeave)
    {
        // A move from one widget to another sends a leave and then an enter: the overlay goes only when no enter
        // follows.
        impl.taskDragLeave.start();
        return true;
    }
    auto* const drag = static_cast<QDropEvent*>(event);
    const QPoint at = widget->mapToGlobal(drag->position().toPoint());
    impl.taskDragLeave.stop();
    if (event->type() == QEvent::Drop)
    {
        // Ignored, so the dock library ends its drag as one not dropped on its own areas and tells so; the task
        // attaches there, from where it was dropped.
        impl.taskDropAt = at;
        drag->setDropAction(Qt::IgnoreAction);
        drag->ignore();
        return true;
    }
    impl.taskDrag->windowDragMoved(at);
    if (impl.taskDrag->zone() != AttachZone::None)
    {
        drag->setDropAction(Qt::MoveAction);
        drag->accept();
    }
    else
    {
        drag->ignore();
    }
    return true;
}

bool ObservatoryWindow::floatTask(const an::ui::shell::TaskId id)
{
    Impl& impl = *m_impl;
    const an::ui::shell::Task* const task = impl.tasks ? impl.tasks->model().task(id) : nullptr;
    if (task == nullptr)
    {
        return false;
    }
    // Over the main window's centre as it is once the page has left it.
    return floatTaskAt(id, QRect());
}

bool ObservatoryWindow::floatTaskAt(const an::ui::shell::TaskId id, const QRect& geometry)
{
    Impl& impl = *m_impl;
    const an::ui::shell::Task* const task = impl.tasks ? impl.tasks->model().task(id) : nullptr;
    TaskPage* const page = taskPage(id);
    if (task == nullptr || page == nullptr || task->process.isNull() ||
        task->presentation != an::ui::shell::TaskPresentation::Page || taskWindow(id) != nullptr)
    {
        return false;
    }
    // The page leaves the centre; the centre shows the place it showed before the task.
    if (impl.centre->currentWidget() == page)
    {
        leaveTaskPage();
        showPlaceBeforeTask();
    }
    impl.centre->removeWidget(page);
    QRect placed = geometry;
    if (placed.isNull())
    {
        // Over the centre of the place now shown, laid out first: the workspace's column takes its share again.
        if (QLayout* const layout = centralWidget()->layout())
        {
            layout->activate();
        }
        placed = QRect(QPoint(0, 0), floatingSizeOf(*task));
        placed.moveCenter(QRect(impl.centre->mapToGlobal(QPoint(0, 0)), impl.centre->size()).center());
    }

    TaskWindow::Hooks hooks;
    hooks.closeRequested = [this, id]() { taskWindowCloseRequested(id); };
    hooks.moved = [this, id]() {
        if (m_impl->taskDrag && m_impl->taskDrag->phase() == TaskDrag::Phase::Window && m_impl->taskDrag->task() == id)
        {
            m_impl->taskDrag->windowDragMoved(QCursor::pos());
        }
    };
    auto window = std::make_unique<TaskWindow>(id, std::move(hooks));
    TaskWindow* const floating = window.get();
    impl.taskWindows[id] = std::move(window);
    impl.floatingProcessNames[id] = QString::fromStdString(task->process->processName());
    floating->show(*page, placed);
    impl.tasks->model().setPresentation(id, an::ui::shell::TaskPresentation::Window);
    refreshTaskHeader(id);
    followFloatingReadout(id);
    impl.appliedTicker.start();
    floating->raiseAndActivate();
    updateTaskActions();
    return true;
}

bool ObservatoryWindow::attachTask(const an::ui::shell::TaskId id)
{
    Impl& impl = *m_impl;
    const auto found = impl.taskWindows.find(id);
    const an::ui::shell::Task* const task = impl.tasks ? impl.tasks->model().task(id) : nullptr;
    if (found == impl.taskWindows.end() || task == nullptr)
    {
        return false;
    }
    // The window's size is the process's from now on; the page's readout is the main window's again.
    rememberFloatingSize(id);
    stopFloatingReadout(id);
    TaskPage* const page = found->second->takePage();
    TaskWindow* const gone = found->second.release();
    impl.taskWindows.erase(found);
    impl.floatingProcessNames.erase(id);
    gone->deleteLater();
    if (page == nullptr)
    {
        return false;
    }
    impl.centre->addWidget(page);
    impl.tasks->model().setPresentation(id, an::ui::shell::TaskPresentation::Page);
    // The current task, its page shown; the main window active and the focus in the page once the window it floated
    // in has gone, which would otherwise take the activation with it.
    (void)impl.tasks->show(id);
    updateTaskActions();
    QPointer<TopLevelProcessBaseControl> process(task->process.data());
    QTimer::singleShot(0, this, [this, process]() {
        if (process.isNull() || !process->isVisible())
        {
            return;
        }
        activateWindow();
        raise();
        // The widget of the process that last had the focus, else its first in the order Tab takes.
        QWidget* focus = process->focusWidget();
        const auto takes = [&process](const QWidget* widget) {
            return widget != nullptr && process->isAncestorOf(widget) && widget->isVisibleTo(process) &&
                   widget->isEnabled() && (widget->focusPolicy() & Qt::TabFocus) != 0;
        };
        for (QWidget* next = process->nextInFocusChain(); !takes(focus) && next != nullptr && next != process.data();
             next = next->nextInFocusChain())
        {
            focus = next;
        }
        (takes(focus) ? focus : static_cast<QWidget*>(process.data()))->setFocus(Qt::OtherFocusReason);
    });
    return true;
}

QSize ObservatoryWindow::floatingSizeOf(const an::ui::shell::Task& task) const
{
    const Impl& impl = *m_impl;
    QSize size;
    if (!task.process.isNull() && impl.core.context() != nullptr)
    {
        int state = Qt::WindowNoState;
        int width = -1;
        int height = -1;
        QString json;
        if (impl.core.context()->getProcessSettings()->select(QString::fromStdString(task.process->processName()),
                                                              state, width, height, json) &&
            width > 0 && height > 0)
        {
            size = QSize(width, height);
        }
    }
    if (!size.isValid())
    {
        // Never given a size: three quarters of the centre.
        size = impl.centre->size() * 3 / 4;
    }
    // Within the screen the main window is on, as a process window's remembered size is, and never under the window's
    // least size: a screen that cannot hold that bounds nothing.
    const QSize least(TaskWindow::kMinimumWidth, TaskWindow::kMinimumHeight);
    if (const QScreen* const screen = this->screen())
    {
        const QSize available = screen->availableGeometry().size();
        if (available.width() >= least.width() && available.height() >= least.height())
        {
            size = size.boundedTo(available);
        }
    }
    return size.expandedTo(least);
}

void ObservatoryWindow::rememberFloatingSize(const an::ui::shell::TaskId id)
{
    Impl& impl = *m_impl;
    const TaskWindow* const floating = taskWindow(id);
    const auto named = impl.floatingProcessNames.find(id);
    if (floating == nullptr || named == impl.floatingProcessNames.end() || impl.core.context() == nullptr)
    {
        return;
    }
    const QSize size = floating->size();
    if (!size.isValid())
    {
        return;
    }
    an::ui::IProcessSettingsProvider* const settings = impl.core.context()->getProcessSettings();
    int state = Qt::WindowNoState;
    int width = -1;
    int height = -1;
    QString json;
    if (!settings->select(named->second, state, width, height, json))
    {
        state = Qt::WindowNoState;
        json.clear();
    }
    // Where a process window's size is kept: its settings stay as they are.
    (void)settings->upsert(named->second, state, size.width(), size.height(), json);
}

void ObservatoryWindow::followFloatingReadout(const an::ui::shell::TaskId id)
{
    Impl& impl = *m_impl;
    const an::ui::shell::Task* const task = impl.tasks ? impl.tasks->model().task(id) : nullptr;
    TaskPage* const page = taskPage(id);
    auto* const preview = task != nullptr ? dynamic_cast<ImageRTPreviewControl*>(task->process.data()) : nullptr;
    if (page == nullptr || preview == nullptr || preview->processSurface() != an::ui::ProcessSurface::PreviewPage)
    {
        return;
    }
    // The page's compact readout reads the task's preview while it floats, as it does in the centre.
    Impl::FloatingReadout readout;
    readout.source = std::make_unique<TaskReadoutSource>(*preview);
    readout.controller = std::make_unique<an::ui::shell::ReadoutController>(impl.core);
    readout.controller->setCompactReadout(page->compactReadout());
    readout.controller->followTask(readout.source.get());
    impl.floatingReadouts[id] = std::move(readout);
}

void ObservatoryWindow::stopFloatingReadout(const an::ui::shell::TaskId id)
{
    Impl& impl = *m_impl;
    const auto found = impl.floatingReadouts.find(id);
    if (found == impl.floatingReadouts.end())
    {
        return;
    }
    if (found->second.controller)
    {
        found->second.controller->followTask(nullptr);
        found->second.controller->setCompactReadout(nullptr);
    }
    found->second.controller.reset();
    found->second.source.reset();
    impl.floatingReadouts.erase(found);
}

void ObservatoryWindow::taskWindowCloseRequested(const an::ui::shell::TaskId id)
{
    Impl& impl = *m_impl;
    // Closing the window closes the task, as its Close does: not during a run, when the window stays.
    if (impl.tasks && taskWindow(id) != nullptr && impl.tasks->canClose(id))
    {
        rememberFloatingSize(id);
        (void)impl.tasks->close(id);
    }
}

an::ui::shell::TaskId ObservatoryWindow::taskOfDock(const KDDockWidgets::QtWidgets::DockWidget* dock) const
{
    if (dock == nullptr)
    {
        return 0u;
    }
    for (const auto& [id, floating] : m_impl->taskWindows)
    {
        if (floating && floating->dockWidget() == dock)
        {
            return id;
        }
    }
    return 0u;
}

void ObservatoryWindow::showAttachZone(const int zone, const an::ui::shell::TaskId id)
{
    Impl& impl = *m_impl;
    QWidget* over = nullptr;
    if (zone == static_cast<int>(AttachZone::Rail))
    {
        over = impl.rail;
    }
    else if (zone == static_cast<int>(AttachZone::Centre))
    {
        over = impl.centre;
    }
    if (over == nullptr)
    {
        impl.attachZone->hide();
    }
    else
    {
        impl.attachZone->setGeometry(QRect(over->mapTo(centralWidget(), QPoint(0, 0)), over->size()));
        impl.attachZone->show();
        impl.attachZone->raise();
    }
    impl.rail->setInsertionMark(id != 0u && over != nullptr ? railTaskId(id) : QString());
}

void ObservatoryWindow::showPlaceBeforeTask()
{
    Impl& impl = *m_impl;
    // The place shown before the task comes back: a workspace while one has views, else Home.
    QWidget* const before = impl.placeBeforeTask.data();
    if (before != nullptr && dynamic_cast<TaskPage*>(before) == nullptr && impl.centre->indexOf(before) >= 0)
    {
        impl.centre->setCurrentWidget(before);
        placeChanged();
    }
    else if (impl.core.viewService().getListOfViewControls().empty())
    {
        showHome();
    }
    else
    {
        showViews();
    }
}

void ObservatoryWindow::updateTaskActions()
{
    Impl& impl = *m_impl;
    if (!impl.shellActions || !impl.tasks)
    {
        return;
    }
    const an::ui::shell::TaskModel& model = impl.tasks->model();
    const an::ui::shell::Task* const current = model.task(model.current());
    const bool floats = current != nullptr && taskWindow(current->id) != nullptr;
    if (QAction* const floatAction = impl.shellActions->action(ids::kFloatTask))
    {
        floatAction->setEnabled(isTaskPageShown() && impl.shownTask != 0u);
    }
    if (QAction* const attach = impl.shellActions->action(ids::kAttachTask))
    {
        attach->setEnabled(floats);
    }
    if (QAction* const closeTask = impl.shellActions->action(ids::kCloseTask))
    {
        closeTask->setEnabled(current != nullptr && impl.tasks->canClose(current->id));
    }
}

} // namespace an::ui::observatory
