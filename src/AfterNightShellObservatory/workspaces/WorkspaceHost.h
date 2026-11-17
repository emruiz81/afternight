// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The Observatory window's workspaces: a dock area and a page each, the place that shows them, and the two dock areas
/// that are never shown and parent the floating windows.

#include "AfterNightShellObservatoryExport.h"

#include "core/IShell.h"
#include "views/DockHooks.h"
#include "views/WorkspaceModel.h"

#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <utility>
#include <vector>

class ImageViewControl;
class QStackedWidget;
class QWidget;

namespace an::ui
{
class KddwProxyStyle;
} // namespace an::ui

namespace an::ui::observatory
{

class WorkspacePage;

/// The workspaces of the window, in the shell core's `WorkspaceModel`. Each has a dock library main window, named
/// `an.workspace.<id>`, made with the shell's window as its parent (made under the page that shows it, it would stay a
/// window of its own), drawn by the decorations' factory and a dock style of its own, and handed to a page the window
/// makes for it; the pages of the workspaces that are not in windows of their own are the pages of one stack, the
/// workspaces' place, which shows the current one. Before any workspace the host makes two main windows inside the
/// shell's window that are never shown: `an.anchor.images`, with no affinity, and `an.anchor.tasks`, with the
/// affinities `an.task` and `an.console`. The dock library parents a floating window to the first main window made
/// that shares its affinity and destroys it with that parent, so a floating image is parented to the first anchor, and
/// the console's and a task's windows to the second, whichever workspace there is or is shown; a main window never
/// shown is never a drop target. A workspace's target names its id, so its lock is its own.
class AFTERNIGHTSHELLOBSERVATORY_API WorkspaceHost final
{
public:
    /// What the host tells the window.
    struct Hooks
    {
        /// A drag in a workspace, or of one of its views out of it, ended: what the workspaces hold may have changed.
        std::function<void()> arrangementChanged;
        /// A drag is about to start: a workspace whose page shows no dock area shows it, so the drag can drop there.
        std::function<void()> dragStarting;
        /// Makes the page a workspace's dock area is shown on; the host owns it from then on.
        std::function<WorkspacePage*(const QString& id, KDDockWidgets::QtWidgets::MainWindow& dockArea)> makePage;
    };

    static constexpr char kImageAnchorName[] = "an.anchor.images";
    static constexpr char kTaskAnchorName[] = "an.anchor.tasks";
    static constexpr char kTaskAffinity[] = "an.task";
    static constexpr char kConsoleAffinity[] = "an.console";
    /// The name the dock library knows a workspace's main window by is this, followed by the workspace's id.
    static constexpr char kWorkspaceNamePrefix[] = "an.workspace.";
    /// The first workspace's.
    static constexpr char kFirstWorkspaceName[] = "an.workspace.1";

    /// Makes the anchors, the workspaces' place and the first workspace, with `shellWindow` as their parent.
    WorkspaceHost(QWidget& shellWindow, Hooks hooks);
    ~WorkspaceHost();

    WorkspaceHost(const WorkspaceHost&) = delete;
    WorkspaceHost& operator=(const WorkspaceHost&) = delete;
    WorkspaceHost(WorkspaceHost&&) = delete;
    WorkspaceHost& operator=(WorkspaceHost&&) = delete;

    [[nodiscard]] an::ui::shell::WorkspaceModel& model() noexcept;
    [[nodiscard]] const an::ui::shell::WorkspaceModel& model() const noexcept;
    /// The workspaces' place: the stack of the pages of the workspaces in the window.
    [[nodiscard]] QStackedWidget* stack() const noexcept;

    /// Adds a workspace at the end, its dock area and its page; it is not made current.
    QString create(const QString& name = {});
    /// Adds a workspace at the end under the id a project recorded it with, so its dock area has the name a saved
    /// layout knows it by; empty, with nothing made, for an empty id or one a workspace has.
    QString createWithId(const QString& id, const QString& name = {});
    /// Closes a workspace: its page and its dock area go. Refused for the only one, and for one in a window of its own.
    bool close(const QString& id);

    /// Makes a workspace the current one - what the layout's actions act on - and shows it on the stack when it is in
    /// the window.
    void setCurrent(const QString& id);
    [[nodiscard]] QString current() const;
    /// The workspace the stack shows: the current one, or the last current one in the window while the current one is
    /// in a window of its own; empty when every workspace is.
    [[nodiscard]] QString shown() const;

    [[nodiscard]] WorkspacePage* page(const QString& id) const;
    [[nodiscard]] KDDockWidgets::QtWidgets::MainWindow* mainWindow(const QString& id) const;
    [[nodiscard]] an::ui::shell::DockTarget target(const QString& id) const;
    [[nodiscard]] QString workspaceName(const QString& id) const;
    /// The current workspace's.
    [[nodiscard]] WorkspacePage* currentPage() const;
    [[nodiscard]] KDDockWidgets::QtWidgets::MainWindow* mainWindow() const;
    [[nodiscard]] an::ui::shell::DockTarget target() const;
    [[nodiscard]] QString workspaceName() const;

    /// The workspace a view is docked in; empty for a view in a floating window, which belongs to none.
    [[nodiscard]] QString workspaceOf(ImageViewControl& view) const;
    /// The views of `views` docked in a workspace.
    [[nodiscard]] std::vector<ImageViewControl*> dockedViews(const QString& id,
                                                             const std::vector<ImageViewControl*>& views) const;
    /// The workspaces' ids, in their order.
    [[nodiscard]] QStringList ids() const;

    /// Takes a workspace's page off the stack, for a window of its own to show; null for an id the host does not hold.
    [[nodiscard]] WorkspacePage* detach(const QString& id);
    /// Puts a workspace's page back on the stack.
    void attach(const QString& id);

    [[nodiscard]] KDDockWidgets::QtWidgets::MainWindow* imageAnchor() const noexcept;
    [[nodiscard]] KDDockWidgets::QtWidgets::MainWindow* taskAnchor() const noexcept;

    /// Draws every workspace's dock area again under the theme now in force.
    void refreshTheme();

private:
    struct Workspace
    {
        QString id;
        QPointer<KDDockWidgets::QtWidgets::MainWindow> mainWindow;
        an::ui::KddwProxyStyle* style = nullptr;
        QPointer<WorkspacePage> page;
    };

    [[nodiscard]] const Workspace* find(const QString& id) const;
    void make(const QString& id);
    void showOnStack(const QString& id);

    QWidget& m_shellWindow;
    Hooks m_hooks;
    an::ui::shell::WorkspaceModel m_model;
    std::vector<Workspace> m_workspaces;
    /// The pages of the workspaces closed, until they are deleted: each still holds a dock area under its id's name.
    std::vector<std::pair<QString, QPointer<WorkspacePage>>> m_closing;
    QPointer<QStackedWidget> m_stack;
    QPointer<KDDockWidgets::QtWidgets::MainWindow> m_imageAnchor;
    QPointer<KDDockWidgets::QtWidgets::MainWindow> m_taskAnchor;
    QString m_shown;
    an::ui::shell::DockHooks::Subscription m_hooksSubscription;
};

} // namespace an::ui::observatory
