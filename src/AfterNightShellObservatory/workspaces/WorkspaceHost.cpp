// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "workspaces/WorkspaceHost.h"

#include "theme/KddwProxyStyle.h"

#include <kddockwidgets/MainWindow.h>

#include <QCoreApplication>
#include <QMargins>
#include <QWidget>

#include <utility>

namespace an::ui::observatory
{

WorkspaceHost::WorkspaceHost(QWidget& shellWindow, Hooks hooks)
{
    // The dock library was configured as the application's core was made (the shell core's DockHooks).
    m_mainWindow = new KDDockWidgets::QtWidgets::MainWindow(QString::fromLatin1(kFirstWorkspaceName),
                                                            KDDockWidgets::MainWindowOptions{}, &shellWindow);
    m_mainWindow->setObjectName(QString::fromLatin1(kFirstWorkspaceName));
    // No margin between the dock area and what surrounds it.
    m_mainWindow->setCenterWidgetMargins(QMargins(0, 0, 0, 0));
    // The dock area's title bars, tabs, separators and groups are drawn from the tokens.
    m_style = an::ui::installKddwProxyStyle(m_mainWindow.data());

    // A drag that ended in the workspace, or took one of its views out of it, may change what the workspace holds.
    an::ui::shell::DockHooks::Subscriber subscriber;
    subscriber.dragEnded = [guarded = m_mainWindow, arrangementChanged = std::move(hooks.arrangementChanged)](
                               const an::ui::shell::DockDrag& drag) {
        if (!arrangementChanged || guarded.isNull())
        {
            return;
        }
        if (drag.mainWindow == guarded.data() || drag.mainWindow == nullptr)
        {
            arrangementChanged();
        }
    };
    m_hooks = an::ui::shell::DockHooks::subscribe(std::move(subscriber));
}

WorkspaceHost::~WorkspaceHost()
{
    m_hooks.reset();
}

KDDockWidgets::QtWidgets::MainWindow* WorkspaceHost::mainWindow() const noexcept
{
    return m_mainWindow.data();
}

an::ui::shell::DockTarget WorkspaceHost::target() const
{
    return an::ui::shell::DockTarget{.mainWindow = m_mainWindow.data(), .workspaceId = {}};
}

QString WorkspaceHost::workspaceName() const
{
    return QCoreApplication::translate("an::ui::observatory::WorkspaceHost", "Workspace 1");
}

void WorkspaceHost::refreshTheme()
{
    if (m_style != nullptr && !m_mainWindow.isNull())
    {
        m_style->refreshThemeTokens();
        m_mainWindow->update();
    }
}

} // namespace an::ui::observatory
