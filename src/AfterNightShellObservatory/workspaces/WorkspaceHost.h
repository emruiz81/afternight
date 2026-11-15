// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The dock areas of the Observatory window's workspaces.

#include "AfterNightShellObservatoryExport.h"

#include "core/IShell.h"
#include "views/DockHooks.h"

#include <QPointer>
#include <QString>

#include <functional>

class QWidget;

namespace an::ui
{
class KddwProxyStyle;
} // namespace an::ui

namespace an::ui::observatory
{

/// The workspaces' dock areas: a dock library main window per workspace, made with the shell's window as its parent
/// (made under the page that shows it, it would stay a window of its own) and handed to that page, drawn by the
/// decorations' factory and the dock style, and joined to the dock library's hooks.
///
/// The window has one workspace: its dock area is the whole layout, as the classic window's is, so its target names no
/// workspace and its lock is the whole layout's.
class AFTERNIGHTSHELLOBSERVATORY_API WorkspaceHost final
{
public:
    /// What the host tells the window.
    struct Hooks
    {
        /// A drag in the workspace, or of one of its views out of it, ended: what the workspace holds may have changed.
        std::function<void()> arrangementChanged;
    };

    /// The name the dock library knows the first workspace's main window by.
    static constexpr char kFirstWorkspaceName[] = "an.workspace.1";

    /// Makes the workspace's main window with `shellWindow` as its parent.
    explicit WorkspaceHost(QWidget& shellWindow, Hooks hooks = {});
    ~WorkspaceHost();

    WorkspaceHost(const WorkspaceHost&) = delete;
    WorkspaceHost& operator=(const WorkspaceHost&) = delete;
    WorkspaceHost(WorkspaceHost&&) = delete;
    WorkspaceHost& operator=(WorkspaceHost&&) = delete;

    /// The workspace's dock area; null once the shell's window destroyed it.
    [[nodiscard]] KDDockWidgets::QtWidgets::MainWindow* mainWindow() const noexcept;

    /// What the core's dock operations act on: the workspace's dock area.
    [[nodiscard]] an::ui::shell::DockTarget target() const;

    /// The workspace's name, as the rail and the breadcrumb say it.
    [[nodiscard]] QString workspaceName() const;

    /// Draws the dock area again under the theme now in force.
    void refreshTheme();

private:
    QPointer<KDDockWidgets::QtWidgets::MainWindow> m_mainWindow;
    an::ui::KddwProxyStyle* m_style = nullptr;
    an::ui::shell::DockHooks::Subscription m_hooks;
};

} // namespace an::ui::observatory
