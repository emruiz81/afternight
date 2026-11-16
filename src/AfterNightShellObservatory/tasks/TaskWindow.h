// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A floating task's window: the dock library's floating window holding the task's page, kept out of every workspace.

#include "AfterNightShellObservatoryExport.h"

#include "tasks/TaskModel.h"

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>

#include <functional>

class QEvent;
class QWidget;

namespace KDDockWidgets::QtWidgets // NOLINT(readability-identifier-naming): the dock library's own namespace.
{
class DockWidget;
} // namespace KDDockWidgets::QtWidgets

namespace an::ui::observatory
{

class TaskPage;

/// The window of a floating task: a floating window of the dock library holding the task's whole page - its header, its
/// canvas and its column - under a title that names the process and its image. Its dock widget has the affinity
/// `an.task`, which no workspace and no image's window shares, so it docks nowhere and is offered nowhere; the layout
/// saver skips it; and it asks for one title bar and no tab strip - the system's title bar where the dock library uses
/// one, the library's own elsewhere. It is a window of its own rather than a tool window, so the main window's
/// shortcuts do not act in it; the page's own do. The library parents it to the main window that is never shown and
/// shares its affinity (`WorkspaceHost::taskAnchor`), which keeps it above the shell's window.
///
/// The person's closing it by its title bar is not the library's to do: the window stays and `closeRequested` asks the
/// window's owner, which closes the task - or does not, during a run. The press on the title bar that closed it is
/// ended at once, so the library starts no drag of a window on its way out. Used on the GUI thread.
class AFTERNIGHTSHELLOBSERVATORY_API TaskWindow final : public QObject
{
public:
    /// The least size of the window.
    static constexpr int kMinimumWidth = 720;
    static constexpr int kMinimumHeight = 480;
    /// The affinity of a task's dock widget.
    static constexpr char kAffinity[] = "an.task";
    /// How far under the window's top the cursor holds a window pulled out of the rail: within its title.
    static constexpr int kGripOffset = 14;

    struct Hooks
    {
        /// The person closed the window by its title bar.
        std::function<void()> closeRequested;
        /// The window moved: a drag of it is on its way, or it was placed.
        std::function<void()> moved;
    };

    TaskWindow(an::ui::shell::TaskId id, Hooks hooks);
    /// The window goes, and the page it still holds with it.
    ~TaskWindow() override;

    TaskWindow(const TaskWindow&) = delete;
    TaskWindow& operator=(const TaskWindow&) = delete;
    TaskWindow(TaskWindow&&) = delete;
    TaskWindow& operator=(TaskWindow&&) = delete;

    /// The dock library's name of a task's dock widget: `an.task.<id>`.
    [[nodiscard]] static QString dockName(an::ui::shell::TaskId id);

    [[nodiscard]] an::ui::shell::TaskId task() const noexcept;

    /// Shows a page in the window at a place on the screen, the window at least its least size.
    void show(TaskPage& page, const QRect& geometry);

    /// Takes the page back out of the window, which closes; null when it holds none. The caller parents the page.
    [[nodiscard]] TaskPage* takePage();

    /// The page the window holds; null while it holds none.
    [[nodiscard]] TaskPage* page() const;

    /// Whether the window is on screen.
    [[nodiscard]] bool isOpen() const;

    /// The window's title: "Curves Transformation - M20_FINAL_PNG_05".
    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;

    /// Brings the window forward and makes it the active one.
    void raiseAndActivate();

    /// The window's size on the screen; empty while it is not on screen.
    [[nodiscard]] QSize size() const;

    /// The dock widget that holds the page, and the floating window it is shown in - null while it is not.
    [[nodiscard]] KDDockWidgets::QtWidgets::DockWidget* dockWidget() const noexcept;
    [[nodiscard]] QWidget* window() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void watchWindow();
    /// Ends the press the dock library waits on after the window's title bar was pressed.
    void endPress();

    an::ui::shell::TaskId m_id = 0;
    Hooks m_hooks;
    KDDockWidgets::QtWidgets::DockWidget* m_dock = nullptr;
    QPointer<TaskPage> m_page;
    QPointer<QWidget> m_watched;
    QSize m_lastSize;
    /// The least size the page had before the window held it, given back with it.
    QSize m_pageMinimum;
};

} // namespace an::ui::observatory
