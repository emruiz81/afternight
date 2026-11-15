// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The Observatory window: the top bar, the rail of places, the centre that shows the current place, the column beside
/// it and the status line.

#include "AfterNightShellObservatoryExport.h"

#include "core/IShell.h"

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <memory>
#include <string>

class ImageViewControl;
class QCloseEvent;
class QResizeEvent;
class QStackedWidget;

namespace an::ui
{
class AnIconRail;
class AnReadoutColumn;
} // namespace an::ui

namespace an::ui::shell
{
class ApplicationCore;
} // namespace an::ui::shell

namespace an::ui::observatory
{

class HomePage;
class MenuBarOnAlt;
class StatusLine;
class TopBar;
class WorkspaceHost;
class WorkspacePage;

/// The Observatory shell's main window. It is what only a window can answer for the application core's services -
/// where a view goes, what the status line shows, whether the Session Console is on screen - and the window a person
/// works in: Home when no image is open, the workspace that holds the views otherwise. Processes open as windows of
/// their own.
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
    /// The column right of the centre, which holds the readout: shown on the workspace, and on Home while an image is
    /// open.
    [[nodiscard]] QWidget* rightColumn() const noexcept;
    [[nodiscard]] an::ui::AnReadoutColumn* readoutColumn() const noexcept;
    [[nodiscard]] StatusLine* statusLine() const noexcept;
    /// The menu bar, hidden until Alt is pressed.
    [[nodiscard]] MenuBarOnAlt& menuBarOnAlt() const noexcept;

    /// The rail's items, by the ids the rail tells them by.
    static constexpr char kRailHome[] = "home";
    static constexpr char kRailWorkspace[] = "workspace.1";
    static constexpr char kRailIntegration[] = "integration";
    static constexpr char kRailConsole[] = "console";
    static constexpr char kRailSettings[] = "settings";

    [[nodiscard]] HomePage* homePage() const noexcept;
    [[nodiscard]] WorkspacePage* workspacePage() const noexcept;
    [[nodiscard]] WorkspaceHost& workspaces() noexcept;

    /// Whether the centre shows Home, or the workspace.
    [[nodiscard]] bool isHomeShown() const;
    [[nodiscard]] bool isWorkspaceShown() const;

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
    /// Shows Home, unless the Session Console holds the workspace's dock area.
    void showHome() override;
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

private:
    void buildRegions();
    void connectDashboard();
    void makeUnits();
    void bindServices();
    void readyFiles();
    void applyThemeById(const QString& themeId);
    void setActionIcons();
    void openFile();
    void openFiles(const QStringList& files);
    void newSequence();
    void openImageIntegration();
    void openSettings();
    void openSettingsAt(const QString& pageName);
    void showConsole(bool visible);
    void arrangementChanged();
    void placeChanged();
    void railActivated(const QString& id);
    void updateRail();
    void updateExtensionMenus();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::observatory
