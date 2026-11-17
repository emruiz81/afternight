// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The Observatory window: the top bar, the rail of places, the centre that shows the current place, the column beside
/// it and the status line.

#include "AfterNightShellObservatoryExport.h"

#include "core/IShell.h"
#include "tasks/TaskModel.h"
#include "views/WorkspaceModel.h"

#include <QMainWindow>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class ImageViewControl;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QPoint;
class QResizeEvent;
class QShowEvent;
class QStackedWidget;

class TopLevelProcessBaseControl;

namespace ImageIntegration
{
class IntegrationJobsModel;
} // namespace ImageIntegration

namespace KDDockWidgets::QtWidgets // NOLINT(readability-identifier-naming): the dock library's own namespace.
{
class DockWidget;
} // namespace KDDockWidgets::QtWidgets

namespace an::ui
{
class AnCommandPalette;
class CompactHistoryDialog;
class AnIconRail;
class AnReadoutColumn;
class AnSnackbar;
struct ProcessRunState;
struct RailItem;
} // namespace an::ui

namespace an::ui::shell
{
class ApplicationCore;
class ProjectLayoutStore;
class ReadoutController;
struct ProjectLayoutRestored;
struct ProjectWorkspace;
} // namespace an::ui::shell

namespace an::ui::observatory
{

class ConsoleDrawer;
class FloatingImagesPage;
class WorkspaceWindow;
class FloatingViewChrome;
class HomePage;
class IntegrationHubPage;
class ProjectHomePage;
class MenuBarOnAlt;
class ProjectFlows;
class ShellActions;
class StatusLine;
class TaskDrag;
class TaskPage;
class TaskPresenter;
class TaskWindow;
class TopBar;
class WorkspaceHost;
class WorkspacePage;
class WorkspaceToolbar;

/// The Observatory shell's main window. It is what only a window can answer for the application core's services -
/// where a view goes, what the status line shows, whether the Session Console is on screen - and the window a person
/// works in: Home when no image is open, the workspace that holds the views otherwise. Every process is a task in the
/// rail (`TaskPresenter`): one whose descriptor states a page opens as a page of the centre (`TaskPage`), the others as
/// windows of their own. A page floats in a window of its own and attaches again (`TaskWindow`), from its header, its
/// rail menu, the palette, or by dragging (`TaskDrag`).
///
/// **A task's page.** While it is shown, the image actions act on the task's target and the looking actions on its
/// canvas (`ViewService::setTaskSubject`); Close, Close All Others and Maximize View are withdrawn; the compact readout
/// follows the task's preview; the breadcrumb names the process and the image, and the status line shows the preview.
/// A view a process makes meanwhile lands in a workspace and does not take the person off the page: a snackbar offers
/// Show. A run that succeeds says so in a snackbar with Undo.
///
/// **Projects.** The top bar's switcher says which project is open and opens the projects' menu; New Project...,
/// Open Project..., Switch Project..., Close Project and Project Settings... are the window's own actions, in the
/// File menu and the palette (`ProjectFlows`). While a project is open, an image opened from a file joins it. As the
/// window starts, the project used last is opened when the settings ask.
///
/// **A project keeps the workspaces.** As a project is left - closed, or given way to another - its workspaces, their
/// names and locks, the images of the project open in each, the ones that float and the arrangement of them all are
/// written into it (`ProjectLayoutStore`), and its images close with it, each asked about as Close asks: one the
/// person keeps open keeps the project open. At exit the same is written. A project opened again gets its workspaces
/// back, its images opened on workers and the arrangement applied to them once they are open; an image whose file is
/// gone is said once, with Locate... and Remove from the project. An image that was open before, which the project
/// does not hold, stays open and floats. With no project nothing is written, and nothing is opened again at the start.
///
/// It is made over an application that is initialized and started (`ApplicationCore::initialize`, `start`): the
/// context exists, the session is open and the installed extensions are known. The core outlives it.
class AFTERNIGHTSHELLOBSERVATORY_API ObservatoryWindow final : public QMainWindow,
                                                               public an::ui::shell::IViewHost,
                                                               public an::ui::shell::IStatusDisplay
{
public:
    /// The least size the window takes.
    static constexpr int kMinimumWidth = 1024;
    static constexpr int kMinimumHeight = 640;

    /// Makes the window and its regions over the core.
    explicit ObservatoryWindow(an::ui::shell::ApplicationCore& core);
    /// Stops what the window's processes still run, destroys them, and lets go of everything the core's services were
    /// handed, before the regions go.
    ~ObservatoryWindow() override;

    ObservatoryWindow(const ObservatoryWindow&) = delete;
    ObservatoryWindow& operator=(const ObservatoryWindow&) = delete;
    ObservatoryWindow(ObservatoryWindow&&) = delete;
    ObservatoryWindow& operator=(ObservatoryWindow&&) = delete;

    /// The window's part of the start: the theme the settings name applied, the image actions withdrawn until an image
    /// is open, the recent files listed. Nothing is shown.
    void initialize();

    // ---- The regions

    [[nodiscard]] TopBar* topBar() const noexcept;
    [[nodiscard]] an::ui::AnIconRail* rail() const noexcept;
    /// The centre: a page per place, kept alive while another is shown.
    [[nodiscard]] QStackedWidget* centre() const noexcept;
    /// The column right of the centre, which holds the readout: shown on the workspace only, not on Home even while an
    /// image is open.
    [[nodiscard]] QWidget* rightColumn() const noexcept;
    [[nodiscard]] an::ui::AnReadoutColumn* readoutColumn() const noexcept;
    [[nodiscard]] StatusLine* statusLine() const noexcept;
    /// The menu bar, hidden until Alt is pressed.
    [[nodiscard]] MenuBarOnAlt& menuBarOnAlt() const noexcept;
    /// The Session Console's drawer, under the centre and the column.
    [[nodiscard]] ConsoleDrawer* consoleDrawer() const noexcept;

    /// The rail's items, by the ids the rail tells them by.
    static constexpr char kRailHome[] = "home";
    static constexpr char kRailWorkspace[] = "workspace.1";
    static constexpr char kRailIntegration[] = "integration";
    static constexpr char kRailConsole[] = "console";
    static constexpr char kRailSettings[] = "settings";

    /// The rail's item of a workspace.
    [[nodiscard]] static QString railWorkspaceId(const QString& workspaceId);

    /// The rail's item of a task.
    [[nodiscard]] static QString railTaskId(an::ui::shell::TaskId id);

    /// The shell's process presenter, which keeps its tasks; null once the window is on its way out.
    [[nodiscard]] TaskPresenter* taskPresenter() const noexcept;

    /// A task's menu: Show, Float as Window - Attach for a floating task, neither offered for a process shown as a
    /// window of its own - and Close (disabled during a run), acting on that task.
    void buildTaskMenu(QMenu& menu, an::ui::shell::TaskId id);

    /// Floats a task's page in a window of its own - Float as Window - at the size its process's window was last given,
    /// over the centre; the rail's item dims and, when the page was shown, the main window shows the place it showed
    /// before the task. Its process, its run and its lock on the image are untouched. False for a task that is no page
    /// in the centre.
    bool floatTask(an::ui::shell::TaskId id);

    /// Brings a floating task's page back into the centre - Attach - as the current task, the focus in it; its window's
    /// size is kept as its process's. False for a task that does not float.
    bool attachTask(an::ui::shell::TaskId id);

    /// The window of a floating task; null for a task that does not float.
    [[nodiscard]] TaskWindow* taskWindow(an::ui::shell::TaskId id) const;

    /// What decides a task's drags between the rail and a window of its own.
    [[nodiscard]] TaskDrag& taskDrag() const noexcept;

    /// The overlay laid over the zone a dragged task's window would attach to - the rail or the centre - hidden while
    /// there is none.
    [[nodiscard]] QWidget* attachZoneOverlay() const noexcept;

    /// The platform's name the task drags decide by - whether a window can follow the cursor - which is the
    /// application's own unless a suite says another, the empty name giving it back.
    void setDragPlatform(const QString& platformName);

    /// The page of a task taken into one; null for a task shown as a window, or for none.
    [[nodiscard]] TaskPage* taskPage(an::ui::shell::TaskId id) const;

    /// Whether the centre shows a task's page, and the task whose page it shows; zero for none.
    [[nodiscard]] bool isTaskPageShown() const;
    [[nodiscard]] an::ui::shell::TaskId shownTask() const noexcept;

    /// The transient message at the foot of the centre.
    [[nodiscard]] an::ui::AnSnackbar* snackbar() const noexcept;

    /// What feeds the readout: the column of the active view, or a task page's compact readout.
    [[nodiscard]] an::ui::shell::ReadoutController& readoutController() const noexcept;

    [[nodiscard]] HomePage* homePage() const noexcept;
    /// Home while a project is open: what the project holds.
    [[nodiscard]] ProjectHomePage* projectHomePage() const noexcept;
    /// The Integration place: the ways to stack, the single steps and the integration jobs.
    [[nodiscard]] IntegrationHubPage* integrationHubPage() const noexcept;
    /// The integration jobs the hub and Project Home show: the sessions Image Integration keeps outside any project and
    /// in the open project's folder, read again as a project opens or closes and as a run of Image Integration ends;
    /// the one an open Image Integration runs shown running.
    [[nodiscard]] ImageIntegration::IntegrationJobsModel& integrationJobs() const noexcept;
    /// The page of the workspace the centre shows, or would show.
    [[nodiscard]] WorkspacePage* workspacePage() const noexcept;
    [[nodiscard]] WorkspaceHost& workspaces() noexcept;
    /// The window of a workspace moved to one of its own; null while it is in this window.
    [[nodiscard]] WorkspaceWindow* workspaceWindow(const QString& workspaceId) const;
    /// The workspace icon's menu: New Workspace, Rename..., Move to New Window, Group All Images, Split All Images,
    /// Fit All Images to Window, Lock Layout and Close Workspace, acting on that workspace.
    void buildWorkspaceMenu(QMenu& menu, const QString& workspaceId);
    /// The tab's menu of a view: the shell core's, with Move to Workspace after Float.
    void buildTabMenu(QMenu& menu, ImageViewControl& view);
    /// The layout icons in the top bar, shown while the workspace is.
    [[nodiscard]] WorkspaceToolbar* layoutIcons() const noexcept;
    /// The small toolbars of the images that float in windows of their own.
    [[nodiscard]] FloatingViewChrome& floatingViewChrome() const noexcept;
    /// The shell's own actions: Home, the Command Palette, the Readout, Night and the densities, the workspace's, the
    /// current task's and the project's.
    [[nodiscard]] ShellActions& shellActions() const noexcept;
    /// What the window does about projects: New, Open, Switch and Close, the switcher's menu, and what is said of a
    /// project that cannot be opened.
    [[nodiscard]] ProjectFlows& projectFlows() const noexcept;
    /// What keeps the open project's workspaces and open images, and puts them back when it is opened again.
    [[nodiscard]] an::ui::shell::ProjectLayoutStore& projectLayout() const noexcept;
    /// Shows the workspace of the open project that was used last: what Continue does. False with no project open.
    bool continueProject();
    /// What Continue is offered as: "Continue", a middle dot and the name of the workspace it shows; empty with no
    /// project open.
    [[nodiscard]] QString continueLabel() const;
    /// The command palette, and the scrim laid over the centre and the column while it is open.
    [[nodiscard]] an::ui::AnCommandPalette* palette() const noexcept;
    [[nodiscard]] QWidget* paletteScrim() const noexcept;
    /// The top bar's More: the menu tree as a menu.
    [[nodiscard]] QMenu* moreMenu() const noexcept;
    /// Opens the command palette over the centre, 12 px under the top bar.
    void openPalette();

    /// Whether the centre shows Home - the project's page while a project is open - or the workspace.
    [[nodiscard]] bool isHomeShown() const;
    [[nodiscard]] bool isProjectHomeShown() const;
    [[nodiscard]] bool isWorkspaceShown() const;
    [[nodiscard]] bool isIntegrationHubShown() const;

    /// Shows the Integration hub: what the rail's Integration and the View menu's Integration do.
    void showIntegrationHub();

    // ---- What the shell asks of its window

    void setSessionConsoleVisible(bool visible);
    [[nodiscard]] bool isSessionConsoleVisible() const;
    /// Draws the window again under the theme now in force: its regions, its dock area, its views and its processes.
    void refreshTheme();
    /// Shows the window where it was the last time, or maximized.
    void showWithSavedGeometry();
    /// Keeps where the window is, for the next start.
    void keepGeometry();

    // ---- Where the views are

    /// Docks a new view beside its neighbour, or on the right of the workspace's dock area; a view maximized goes back
    /// to the dock area first.
    void place(ImageViewControl& view, const an::ui::shell::ViewPlacement& placement) override;
    /// Brings the view's dock forward.
    void activate(ImageViewControl& view) override;
    void viewAdded(ImageViewControl& view, an::ui::shell::ViewOrigin origin) override;
    void viewRemoved(ImageViewControl& view) override;
    void viewRenamed(ImageViewControl& view, const std::string& previousName) override;
    /// Shows the view over the whole workspace page, the layout locked while it is.
    void maximize(ImageViewControl& view) override;
    void restore(ImageViewControl& view) override;
    [[nodiscard]] bool isMaximizedViewActive() const override;
    /// The workspace's dock area: the window has one.
    [[nodiscard]] an::ui::shell::DockTarget dockTarget(const ImageViewControl* view = nullptr) override;
    /// Shows Home: the project's page while a project is open.
    void showHome() override;

    /// Opens the Compact history dialog over the open project's images, once their history is measured.
    void openCompactHistory();
    /// The Compact history dialog while it is open; null otherwise.
    [[nodiscard]] an::ui::CompactHistoryDialog* compactHistoryDialog() const noexcept;
    /// Shows the workspace.
    void showViews() override;

    // ---- The status line

    void showImageInfo(ImageViewControl* view) override;
    void showZoom(ImageViewControl* view) override;
    void showCelestialCoordinates(double raDegrees, double decDegrees) override;
    void clearCelestialCoordinates() override;
    void clearImageInfo() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildRegions();
    void connectHome();
    void makeUnits();
    void bindServices();
    void readyFiles();
    void applyThemeById(const QString& themeId);
    void setActionIcons();
    void openFile();
    void openFiles(const QStringList& files);
    void newSequence();
    void openImageIntegration();
    /// Opens Image Integration on a job's session: the one open is shown on it, or one is launched and shown on it.
    void openIntegrationJob(const QString& databasePath, const QString& session);
    /// Brings forward the task of the Image Integration that runs a job.
    void showIntegrationJob(const QString& databasePath, const QString& session);
    /// Where the integration jobs are read from: the database outside any project and the open project's.
    void updateIntegrationSources();
    /// Which jobs the open Image Integration windows run now, as their run states say.
    void updateRunningIntegrationJobs();
    /// Tells Project Home what the window holds of the project: its workspaces, where its images are open, their
    /// pictures; and reads the project again.
    void refreshProjectHome();
    /// The project that opened: its page shows on Home, and a heavy history asks once to be compacted.
    void projectOpened();
    /// The projects used last changed: Home shows them.
    void refreshRecentProjects();
    void openSettings();
    void openSettingsAt(const QString& pageName);
    void showConsole(bool visible);
    /// The drawer opened, closed, floated, attached or resized: the catalogue's action follows it and the settings
    /// keep it.
    void consoleChanged();
    /// The count of the warnings not seen, on the rail's Console item and the status line's chip.
    void consoleUnseen(int unseen);
    /// The drawer as the last start left it.
    void restoreConsole();
    void arrangementChanged();
    void placeChanged();
    void railActivated(const QString& id);
    void updateRail();
    /// The tasks' items of the rail, in the order they were opened.
    void appendTaskItems(std::vector<an::ui::RailItem>& items) const;
    /// What each workspace's history strip shows of the task open on the view it shows.
    void refreshPendingChips();
    void updateExtensionMenus();
    void activeViewChanged(ImageViewControl* view);
    void refreshViewSelector();
    void showViewMenu(ImageViewControl& view, const QPoint& globalPos);
    bool showTabMenu(QWidget& tabBar, const QPoint& position, const QPoint& globalPos);
    /// Docks a view into a workspace, the current one for none named.
    void dockIntoWorkspace(ImageViewControl& view, const QString& workspaceId = {});
    /// What each workspace's page shows: its dock area, or its quiet page; and which view its chrome shows.
    void updateWorkspacePage();
    [[nodiscard]] WorkspacePage* makeWorkspacePage(const QString& id, KDDockWidgets::QtWidgets::MainWindow& dockArea);
    [[nodiscard]] WorkspacePage* pageOf(ImageViewControl& view) const;
    void workspacesChanged(an::ui::shell::WorkspaceModel::Change change, const QString& id);
    /// Closes every workspace but one that has no docked view and that the person left.
    void closeLeftEmptyWorkspaces();
    void showDockAreasForADrag();
    void showWorkspace(const QString& id);
    void newWorkspace();
    void renameWorkspace(const QString& id);
    void moveWorkspaceToNewWindow(const QString& id);
    void returnWorkspace(const QString& id);
    void closeWorkspace(const QString& id);
    /// Closes the views docked in a workspace as Close closes each, asked about unsaved changes as it is; true when
    /// none is left.
    bool closeViewsOf(const QString& id);
    void lockWorkspace(const QString& id, bool locked);
    void moveViewToWorkspace(ImageViewControl& view, const QString& id);
    void updateWorkspaceActions();
    void offerTheLock(bool maximized);
    void lockLayout(bool locked);
    void buildTopBarTrailing();
    void refreshChromeIcons();
    void bindShellActions();
    void updateBreadcrumb();
    void setNight(bool night);
    void setDensity(bool comfortable);
    /// Lays a process into a page of the centre: false leaves it a window.
    bool adoptTaskPage(TopLevelProcessBaseControl& process);
    /// Shows a task's page: the place shown before is kept to go back to, the subject and the readout follow the task.
    void showTaskPage(an::ui::shell::TaskId id);
    /// The centre shows no task's page any more: the actions and the readout follow the active view again.
    void leaveTaskPage();
    /// A task's run moved on: its header's state, and the snackbar of a run that succeeded.
    void taskRunChanged(an::ui::shell::TaskId id, const an::ui::ProcessRunState& before);
    /// The pages of the tasks the model holds: a page whose task is gone goes, and the place shown before comes back.
    void refreshTaskPages();
    /// What a task's header says: its image, its name and where its run stands.
    void refreshTaskHeader(an::ui::shell::TaskId id);
    /// Shows a transient message; `act` is what its action does.
    void showSnackbar(const QString& text, const QString& detail, const QString& action, std::function<void()> act);
    void placeSnackbar();
    /// Runs the current task's primary action.
    void applyCurrentTask();
    void applyCaptionColour();
    void placePaletteScrim();
    /// Floats a task's page in its window, over a place on the screen; an empty one places it over the centre.
    bool floatTaskAt(an::ui::shell::TaskId id, const QRect& geometry);
    /// The size a task's window opens at: the size its process's window was last given, within the screen, or the
    /// centre's least it can be.
    [[nodiscard]] QSize floatingSizeOf(const an::ui::shell::Task& task) const;
    /// Keeps a floating task's window size as its process's.
    void rememberFloatingSize(an::ui::shell::TaskId id);
    /// The compact readout of a floating task's page follows its preview, and stops.
    void followFloatingReadout(an::ui::shell::TaskId id);
    void stopFloatingReadout(an::ui::shell::TaskId id);
    /// The person closed a floating task's window by its title bar.
    void taskWindowCloseRequested(an::ui::shell::TaskId id);
    /// The task whose window holds a dock widget; zero for none.
    [[nodiscard]] an::ui::shell::TaskId taskOfDock(const KDDockWidgets::QtWidgets::DockWidget* dock) const;
    /// Covers a zone with the attach overlay, and marks a task's place in the rail.
    void showAttachZone(int zone, an::ui::shell::TaskId id);
    /// Where the compositor alone moves a window (Wayland) the dock library drags a task's window as a QDrag: the
    /// window stays where it is, and the drag's own events - which reach the widgets of this window under the pointer -
    /// say where it stands. Reads them while a task's window is dragged there; true when the event was a task drag's.
    [[nodiscard]] bool taskWindowDragEvent(QObject* watched, QEvent* event);
    /// The place the centre showed before the first task's page: a workspace while one has views, else Home.
    void showPlaceBeforeTask();
    /// Float as Window, Attach and Close Task follow the current task.
    void updateTaskActions();
    /// A view's tools are outside it in this shell, and what the workspaces show follows it as it floats and docks.
    void adoptView(ImageViewControl& view);
    /// The open project is left - closed, or given way to another: its arrangement is kept and its images close, each
    /// asked about as Close asks. False keeps the project: an image of it stays open, or its images are still opening.
    [[nodiscard]] bool leaveProject();
    /// Makes the workspaces the ones a project lists, each under its id and its name; a workspace it does not list
    /// goes, its views floating.
    void takeProjectWorkspaces(const std::vector<an::ui::shell::ProjectWorkspace>& listed);
    /// Opens a project's images as files are opened, on workers, and tells `done` once every one is shown or failed.
    void openProjectImages(const QStringList& files, std::function<void()> done);
    /// Docks a view of a project the saved arrangement did not place: in its workspace, as a new view is.
    void dockRestoredView(ImageViewControl& view, const QString& workspaceId);
    /// A project's arrangement is back: its workspace used last is shown, and what could not be opened is said.
    void projectLayoutRestored(const an::ui::shell::ProjectLayoutRestored& restored);
    /// Opens an image of the open project in the workspace the project lists it in.
    void openProjectImage(const QString& file, const QString& imageId);

    class DragSurface;
    friend class DragSurface;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::observatory
