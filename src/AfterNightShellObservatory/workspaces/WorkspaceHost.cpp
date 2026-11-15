// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "workspaces/WorkspaceHost.h"

#include "pages/WorkspacePage.h"
#include "views/DockLayoutOperations.h"

#include "controls/ImageViewControl.h"
#include "theme/KddwProxyStyle.h"

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/MainWindow.h>

#include <QCoreApplication>
#include <QMargins>
#include <QStackedWidget>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace an::ui::observatory
{

namespace
{

/// A main window of the dock library inside the shell's window that is never shown.
[[nodiscard]] KDDockWidgets::QtWidgets::MainWindow* makeAnchor(const char* name, const QStringList& affinities,
                                                               QWidget& shellWindow)
{
    auto* anchor = new KDDockWidgets::QtWidgets::MainWindow(QString::fromLatin1(name),
                                                            KDDockWidgets::MainWindowOptions{}, &shellWindow);
    anchor->setObjectName(QString::fromLatin1(name));
    if (!affinities.isEmpty())
    {
        anchor->setAffinities(affinities);
    }
    // A main window of the library is a window of its own until a layout takes it: an anchor is a child of the shell's
    // window that is never shown, so it takes no activation and no drop.
    anchor->setWindowFlags(Qt::Widget);
    anchor->hide();
    return anchor;
}

} // namespace

WorkspaceHost::WorkspaceHost(QWidget& shellWindow, Hooks hooks) : m_shellWindow(shellWindow), m_hooks(std::move(hooks))
{
    // The dock library was configured as the application's core was made (the shell core's DockHooks). The anchors
    // come first: the library parents a floating window to the first main window made that shares its affinity.
    m_imageAnchor = makeAnchor(kImageAnchorName, {}, shellWindow);
    m_taskAnchor = makeAnchor(kTaskAnchorName,
                              {QString::fromLatin1(kTaskAffinity), QString::fromLatin1(kConsoleAffinity)}, shellWindow);

    m_stack = new QStackedWidget();
    m_stack->setObjectName(QStringLiteral("observatoryWorkspaces"));

    // A drag that ended in a workspace, or took one of its views out of it, may change what the workspaces hold; a
    // drag about to start shows the dock area of a workspace that shows none, so the drag can land there.
    an::ui::shell::DockHooks::Subscriber subscriber;
    subscriber.dragAboutToStart = [this](const an::ui::shell::DockDrag& /*drag*/) {
        if (m_hooks.dragStarting)
        {
            m_hooks.dragStarting();
        }
        return true;
    };
    subscriber.dragEnded = [this](const an::ui::shell::DockDrag& drag) {
        if (!m_hooks.arrangementChanged)
        {
            return;
        }
        const bool ours = drag.mainWindow == nullptr ||
                          std::any_of(m_workspaces.begin(), m_workspaces.end(), [&drag](const Workspace& workspace) {
                              return workspace.mainWindow.data() == drag.mainWindow;
                          });
        if (ours)
        {
            m_hooks.arrangementChanged();
        }
    };
    m_hooksSubscription = an::ui::shell::DockHooks::subscribe(std::move(subscriber));

    const QString first = m_model.create();
    make(first);
    showOnStack(first);
}

WorkspaceHost::~WorkspaceHost()
{
    m_hooksSubscription.reset();
    m_hooks = {};
}

an::ui::shell::WorkspaceModel& WorkspaceHost::model() noexcept
{
    return m_model;
}

const an::ui::shell::WorkspaceModel& WorkspaceHost::model() const noexcept
{
    return m_model;
}

QStackedWidget* WorkspaceHost::stack() const noexcept
{
    return m_stack.data();
}

QString WorkspaceHost::create(const QString& name)
{
    const QString id = m_model.create(name);
    make(id);
    return id;
}

bool WorkspaceHost::close(const QString& id)
{
    const an::ui::shell::Workspace* const workspace = m_model.find(id);
    if (workspace == nullptr || workspace->detached || !m_model.canClose(id))
    {
        return false;
    }
    const auto found = std::find_if(m_workspaces.begin(), m_workspaces.end(),
                                    [&id](const Workspace& entry) { return entry.id == id; });
    QPointer<WorkspacePage> page = found != m_workspaces.end() ? found->page : QPointer<WorkspacePage>();
    if (found != m_workspaces.end())
    {
        m_workspaces.erase(found);
    }
    (void)m_model.close(id);
    if (m_shown == id)
    {
        m_shown.clear();
    }
    // The stack shows the current workspace, or another in the window when the current one is in a window of its own.
    const an::ui::shell::Workspace* const current = m_model.find(m_model.current());
    if (current != nullptr && !current->detached)
    {
        showOnStack(current->id);
    }
    else
    {
        for (const an::ui::shell::Workspace& other : m_model.workspaces())
        {
            if (!other.detached)
            {
                showOnStack(other.id);
                break;
            }
        }
    }
    // The page and its dock area go once what closed the workspace has returned: a menu of its own may have.
    if (!page.isNull())
    {
        if (m_stack != nullptr)
        {
            m_stack->removeWidget(page.data());
        }
        page->hide();
        page->deleteLater();
    }
    return true;
}

void WorkspaceHost::setCurrent(const QString& id)
{
    const an::ui::shell::Workspace* const workspace = m_model.find(id);
    if (workspace == nullptr)
    {
        return;
    }
    (void)m_model.setCurrent(id);
    if (!workspace->detached)
    {
        showOnStack(id);
    }
}

QString WorkspaceHost::current() const
{
    return m_model.current();
}

QString WorkspaceHost::shown() const
{
    return m_shown;
}

WorkspacePage* WorkspaceHost::page(const QString& id) const
{
    const Workspace* const workspace = find(id);
    return workspace != nullptr ? workspace->page.data() : nullptr;
}

KDDockWidgets::QtWidgets::MainWindow* WorkspaceHost::mainWindow(const QString& id) const
{
    const Workspace* const workspace = find(id);
    return workspace != nullptr ? workspace->mainWindow.data() : nullptr;
}

an::ui::shell::DockTarget WorkspaceHost::target(const QString& id) const
{
    return an::ui::shell::DockTarget{.mainWindow = mainWindow(id), .workspaceId = id};
}

QString WorkspaceHost::workspaceName(const QString& id) const
{
    const an::ui::shell::Workspace* const workspace = m_model.find(id);
    return workspace != nullptr ? workspace->name : QString();
}

WorkspacePage* WorkspaceHost::currentPage() const
{
    return page(m_model.current());
}

KDDockWidgets::QtWidgets::MainWindow* WorkspaceHost::mainWindow() const
{
    return mainWindow(m_model.current());
}

an::ui::shell::DockTarget WorkspaceHost::target() const
{
    return target(m_model.current());
}

QString WorkspaceHost::workspaceName() const
{
    return workspaceName(m_model.current());
}

QString WorkspaceHost::workspaceOf(ImageViewControl& view) const
{
    for (const Workspace& workspace : m_workspaces)
    {
        if (!workspace.mainWindow.isNull() &&
            !an::ui::shell::DockLayoutOperations::viewsDockedIn(target(workspace.id), {&view}).empty())
        {
            return workspace.id;
        }
    }
    return {};
}

std::vector<ImageViewControl*> WorkspaceHost::dockedViews(const QString& id,
                                                          const std::vector<ImageViewControl*>& views) const
{
    return an::ui::shell::DockLayoutOperations::viewsDockedIn(target(id), views);
}

QStringList WorkspaceHost::ids() const
{
    QStringList ordered;
    for (const an::ui::shell::Workspace& workspace : m_model.workspaces())
    {
        ordered.append(workspace.id);
    }
    return ordered;
}

WorkspacePage* WorkspaceHost::detach(const QString& id)
{
    WorkspacePage* const taken = page(id);
    if (taken == nullptr || m_stack == nullptr)
    {
        return nullptr;
    }
    m_stack->removeWidget(taken);
    (void)m_model.setDetached(id, true);
    if (m_shown == id)
    {
        m_shown.clear();
        for (const an::ui::shell::Workspace& other : m_model.workspaces())
        {
            if (!other.detached)
            {
                showOnStack(other.id);
                break;
            }
        }
    }
    return taken;
}

void WorkspaceHost::attach(const QString& id)
{
    WorkspacePage* const returned = page(id);
    if (returned == nullptr || m_stack == nullptr)
    {
        return;
    }
    (void)m_model.setDetached(id, false);
    if (m_stack->indexOf(returned) < 0)
    {
        m_stack->addWidget(returned);
    }
    if (m_shown.isEmpty())
    {
        showOnStack(id);
    }
}

KDDockWidgets::QtWidgets::MainWindow* WorkspaceHost::imageAnchor() const noexcept
{
    return m_imageAnchor.data();
}

KDDockWidgets::QtWidgets::MainWindow* WorkspaceHost::taskAnchor() const noexcept
{
    return m_taskAnchor.data();
}

void WorkspaceHost::refreshTheme()
{
    for (const Workspace& workspace : m_workspaces)
    {
        if (workspace.style != nullptr && !workspace.mainWindow.isNull())
        {
            workspace.style->refreshThemeTokens();
            workspace.mainWindow->update();
        }
    }
}

const WorkspaceHost::Workspace* WorkspaceHost::find(const QString& id) const
{
    const auto found = std::find_if(m_workspaces.begin(), m_workspaces.end(),
                                    [&id](const Workspace& workspace) { return workspace.id == id; });
    return found != m_workspaces.end() ? &*found : nullptr;
}

void WorkspaceHost::make(const QString& id)
{
    Workspace workspace;
    workspace.id = id;
    const QString name = QString::fromLatin1(kWorkspaceNamePrefix) + id;
    workspace.mainWindow =
        new KDDockWidgets::QtWidgets::MainWindow(name, KDDockWidgets::MainWindowOptions{}, &m_shellWindow);
    workspace.mainWindow->setObjectName(name);
    // No margin between the dock area and what surrounds it.
    workspace.mainWindow->setCenterWidgetMargins(QMargins(0, 0, 0, 0));
    // The dock area's title bars, tabs, separators and groups are drawn from the tokens; each area keeps its own style,
    // which a change of theme refreshes.
    workspace.style = an::ui::installKddwProxyStyle(workspace.mainWindow.data());
    if (m_hooks.makePage)
    {
        workspace.page = m_hooks.makePage(id, *workspace.mainWindow);
    }
    if (!workspace.page.isNull() && m_stack != nullptr)
    {
        m_stack->addWidget(workspace.page.data());
    }
    m_workspaces.push_back(std::move(workspace));
}

void WorkspaceHost::showOnStack(const QString& id)
{
    WorkspacePage* const shown = page(id);
    if (shown == nullptr || m_stack == nullptr || m_stack->indexOf(shown) < 0)
    {
        return;
    }
    m_shown = id;
    m_stack->setCurrentWidget(shown);
}

} // namespace an::ui::observatory
