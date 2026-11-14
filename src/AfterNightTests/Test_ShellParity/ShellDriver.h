// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What a parity scenario does to a shell: the handful of things a person does to the application, each done as the
/// shell lets a person do it. A scenario is written once against this and run for every registered shell; a shell
/// that cannot do one of them yet says so, and the scenario fails until it can.

#include <QString>
#include <QStringList>

#include <optional>
#include <string_view>

class ImageViewControl;
class QWidget;

namespace an::tests::parity
{

/// What launching a process came to: the window or page that shows it, or what a person reads instead.
struct ProcessLaunch
{
    /// The window or page that shows the process; null when the launch was refused or showed nothing.
    QWidget* window = nullptr;
    /// Why the process cannot run, as the shell tells a person; empty when it was not refused.
    QString refusal;
};

class ShellDriver
{
public:
    virtual ~ShellDriver() = default;

    /// The shell's main window.
    [[nodiscard]] virtual QWidget& window() = 0;

    /// Opens a file as a person opens one in this shell; the view the open made, or null when none came.
    virtual ImageViewControl* openImage(const QString& path) = 0;
    /// The view the image actions act on; null when there is none.
    [[nodiscard]] virtual ImageViewControl* activeView() = 0;
    /// The names of the views the shell holds, sorted.
    [[nodiscard]] virtual QStringList viewNames() = 0;
    /// Closes a view as a person does, answering Yes to the question an image with unsaved changes asks; the title of
    /// the question it answered, or empty when none was asked.
    virtual QString closeView(ImageViewControl& view) = 0;
    /// Whether a person may drag the view out of its place now.
    [[nodiscard]] virtual bool canDragView(ImageViewControl& view) = 0;

    /// Whether a person can reach one of the catalogue's actions from the shell's window: a menu, a tool bar, the
    /// palette or a view's menu.
    [[nodiscard]] virtual bool reaches(std::string_view actionId) = 0;
    /// Whether the shell offers one of the catalogue's actions enabled where a person reaches it; nothing when it
    /// offers it nowhere.
    [[nodiscard]] virtual std::optional<bool> offeredEnabled(std::string_view actionId) = 0;
    /// Runs one of the catalogue's actions as the shell offers it; false when the shell offers it nowhere.
    virtual bool runAction(std::string_view actionId) = 0;

    /// Launches a process by its registry id as a person launches it in this shell.
    virtual ProcessLaunch launchProcess(std::string_view processId) = 0;
    /// Whether the process's window or page offers Apply now.
    [[nodiscard]] virtual bool applyOffered(QWidget& process) = 0;
    /// Presses Apply in a process's window or page as a person does, once the process offers it; whether it was
    /// pressed.
    virtual bool pressApply(QWidget& process) = 0;
    /// Whether a run of the process is in progress, as the shell shows it.
    [[nodiscard]] virtual bool runInProgress(QWidget& process) = 0;
    /// Cancels the process's run in progress as a person does; whether there was one to cancel.
    virtual bool cancelRun(QWidget& process) = 0;

    /// What the shell's status line reads, all of it.
    [[nodiscard]] virtual QString statusText() = 0;
    /// The entries of the menu a view shows on a right click, as the record of the view's menu writes them.
    [[nodiscard]] virtual QStringList viewMenuEntries(ImageViewControl& view) = 0;

    /// Drops files on the shell's window as a person drops them; whether the window took the drop.
    virtual bool dropFiles(const QStringList& files) = 0;
    /// Closes the shell's window as a person does, answering every question it asks with Yes; whether it closed.
    virtual bool closeWindow() = 0;

    /// Puts the shell back between scenarios: every dialog and process window closed, every view gone, the layout
    /// unlocked.
    virtual void reset() = 0;
};

} // namespace an::tests::parity
