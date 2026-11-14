// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The shell contract: what a shell is to the application that makes it.
///
/// A shell is a window and the few answers only a window can give - where a new view goes, what the status line
/// shows, whether the Session Console is on screen. Everything else - the stores, the views' registry, the actions,
/// the processes - belongs to the application without a window, which the shell is made over. The application knows
/// a shell before it makes one through its `ShellDescriptor`, and makes exactly one per run.
///
/// The types the contract names are declared here rather than included, so a unit that includes it reaches no
/// widget, dock library or process header through it.

#include "AfterNightShellCoreExport.h"

#include <QString>

#include <functional>
#include <memory>
#include <string>

class ImageViewControl;
class QWidget;

namespace KDDockWidgets::QtWidgets // NOLINT(readability-identifier-naming): the dock library's own namespace.
{
class MainWindow;
} // namespace KDDockWidgets::QtWidgets

namespace an::ui
{
class IProcessPresenter;
} // namespace an::ui

namespace an::ui::shell
{

class ApplicationCore;

/// Who asked for a new view.
enum class ViewOrigin
{
    User, ///< an action, the palette, a dropped or a recent file: the user is taken to the view
    Process, ///< a result of a process, a run or an extension: the place on screen is the shell's to keep
};

/// Where a new view goes.
struct ViewPlacement
{
    ImageViewControl* neighbour = nullptr; ///< dock beside it or as its tab; null: the shell's default
    bool keepFocusedNeighbour = false;
};

/// The dock area an operation acts on: a workspace's, or the one a view is in.
struct DockTarget
{
    KDDockWidgets::QtWidgets::MainWindow* mainWindow = nullptr;
    QString workspaceId; ///< empty in a shell with one dock area
};

/// The shell's answers about the views it holds on screen.
class AFTERNIGHTSHELLCORE_API IViewHost
{
public:
    virtual ~IViewHost() = default;

    virtual void place(ImageViewControl& view, const ViewPlacement& placement) = 0;
    virtual void activate(ImageViewControl& view) = 0;
    virtual void viewAdded(ImageViewControl& view, ViewOrigin origin) = 0;
    virtual void viewRemoved(ImageViewControl& view) = 0;
    /// The view is listed under a new name; `previousName` is the one it was listed under.
    virtual void viewRenamed(ImageViewControl& view, const std::string& previousName) = 0;
    virtual void maximize(ImageViewControl& view) = 0;
    virtual void restore(ImageViewControl& view) = 0;
    [[nodiscard]] virtual bool isMaximizedViewActive() const = 0;
    /// The dock area on screen, or the one holding `view` when it is given.
    [[nodiscard]] virtual DockTarget dockTarget(const ImageViewControl* view = nullptr) = 0;
    /// Shows the place for "no image is open".
    virtual void showHome() = 0;
    /// Shows the place that holds views. Asked after `viewAdded` for a view of `ViewOrigin::User` only.
    virtual void showViews() = 0;
};

/// What the shell shows of the active image, the zoom and the position under the cursor.
class AFTERNIGHTSHELLCORE_API IStatusDisplay
{
public:
    virtual ~IStatusDisplay() = default;

    virtual void showImageInfo(ImageViewControl* view) = 0;
    virtual void showZoom(ImageViewControl* view) = 0;
    virtual void showCelestialCoordinates(double raDegrees, double decDegrees) = 0;
    virtual void clearCelestialCoordinates() = 0;
    virtual void clearImageInfo() = 0;
};

/// A shell: the window, and what only the window can answer.
class AFTERNIGHTSHELLCORE_API IShell
{
public:
    virtual ~IShell() = default;

    /// Creates the window and its parts. Nothing is shown.
    virtual void build() = 0;
    [[nodiscard]] virtual QWidget* mainWindow() = 0;
    [[nodiscard]] virtual IViewHost& viewHost() = 0;
    [[nodiscard]] virtual IStatusDisplay& statusDisplay() = 0;
    /// Null in a shell where every process is a window.
    [[nodiscard]] virtual an::ui::IProcessPresenter* processPresenter() = 0;
    virtual void setSessionConsoleVisible(bool visible) = 0;
    [[nodiscard]] virtual bool isSessionConsoleVisible() const = 0;
    virtual void refreshTheme() = 0;
    virtual void showWithSavedGeometry() = 0;
    virtual void saveGeometry() = 0;
};

/// What the application knows of a shell before it makes one.
struct ShellDescriptor
{
    /// The name the shell is chosen by: on the command line (`--shell=<id>`), in the environment and in the setting.
    /// A lowercase word: `classic`, `observatory`.
    QString id;
    /// The name the user reads where shells are listed.
    QString displayName;
    /// Makes the shell over the application. Called once, when the shell has been chosen; nothing is built or shown
    /// until the application asks the shell to.
    std::function<std::unique_ptr<IShell>(ApplicationCore&)> create;
};

} // namespace an::ui::shell
