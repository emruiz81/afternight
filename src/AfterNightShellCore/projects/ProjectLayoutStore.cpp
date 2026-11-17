// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "projects/ProjectLayoutStore.h"

#include "core/ApplicationCore.h"
#include "projects/ProjectHistoryBinder.h"
#include "projects/ProjectService.h"
#include "views/DockHooks.h"
#include "views/DockLayoutOperations.h"
#include "views/ViewService.h"
#include "views/WorkspaceModel.h"

#include "controls/ImageViewControl.h"

#include <AfterNightPlatform/Logging.h>

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/core/DockWidget.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

namespace an::ui::shell
{

namespace
{

/// The most bytes a project's layout is: a larger file is not read.
constexpr qint64 kMaxLayoutBytes = qint64{16} * 1024 * 1024;

/// The longest name a project's document takes of a workspace.
constexpr qsizetype kMaxWorkspaceNameLength = 255;

/// A path as the project records it.
[[nodiscard]] QString cleaned(const QString& path)
{
    return path.trimmed().isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
}

[[nodiscard]] bool samePath(const QString& left, const QString& right)
{
#ifdef Q_OS_WIN
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

/// The bytes of a project's layout; empty for a file that is not there, cannot be read or is larger than a layout is.
[[nodiscard]] QByteArray readLayout(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxLayoutBytes)
    {
        return {};
    }
    const QByteArray bytes = file.read(kMaxLayoutBytes + 1);
    return file.error() == QFileDevice::NoError && bytes.size() <= kMaxLayoutBytes ? bytes : QByteArray();
}

/// Writes a project's layout whole: to a file beside it that takes its place once complete.
[[nodiscard]] bool writeLayout(const QString& filePath, const QByteArray& layout)
{
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath()))
    {
        return false;
    }
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(layout) != layout.size())
    {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

/// Whether a view's dock widget is docked in a dock area or floats in a window: placed, either way.
[[nodiscard]] bool isPlaced(ImageViewControl& view)
{
    KDDockWidgets::QtWidgets::DockWidget* const dock = view.getDockWidget();
    KDDockWidgets::Core::DockWidget* const controller = dock != nullptr ? dock->dockWidget() : nullptr;
    return controller != nullptr && (controller->isInMainWindow() || controller->floatingWindow() != nullptr);
}

/// An image the restore opens, and the view made for it once it arrives.
struct Pending
{
    QString imageId;
    QString path;

    /// The name its dock widget had in the saved layout.
    QString dockWidget;

    /// The workspace the project lists it in; empty for one that floated.
    QString workspaceId;

    QPointer<ImageViewControl> view;
    bool arrived = false;
};

/// One restore, from the project's record to the layout applied.
struct Run
{
    std::uint64_t generation = 0;
    QString folder;
    std::vector<ProjectWorkspace> workspaces;
    QByteArray layout;
    std::vector<Pending> pending;
    std::vector<ProjectMissingImage> missing;
    QString lastWorkspace;
};

} // namespace

struct ProjectLayoutStore::Impl
{
    Impl(ApplicationCore& application, WorkspaceModel& model, Hooks shellHooks)
        : core(application), workspaces(model), hooks(std::move(shellHooks))
    {
    }

    ApplicationCore& core;
    WorkspaceModel& workspaces;
    Hooks hooks;

    bool restoring = false;
    std::uint64_t lastGeneration = 0;
    Run run;

    // Says, to the end of a restore that comes later, whether the store is still there.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    /// The view a project lists, when it is one the project can list: a standard view of an image of the project that
    /// no view listed before it shows.
    [[nodiscard]] std::optional<ProjectWorkspaceView> listedView(ImageViewControl* view, QSet<QString>& listed)
    {
        if (view == nullptr || view->getViewControlMode() != ViewControlMode::standard)
        {
            return std::nullopt;
        }
        const QString image = core.projectHistory().imageOf(*view);
        if (image.isEmpty() || listed.contains(image) || core.projects().image(image) == nullptr)
        {
            return std::nullopt;
        }
        KDDockWidgets::QtWidgets::DockWidget* const dock = view->getDockWidget();
        if (dock == nullptr)
        {
            return std::nullopt;
        }
        listed.insert(image);
        return ProjectWorkspaceView{image, dock->uniqueName()};
    }

    void finish(std::uint64_t generation);
};

ProjectLayoutStore::ProjectLayoutStore(ApplicationCore& core, WorkspaceModel& workspaces, Hooks hooks)
    : m_impl(std::make_unique<Impl>(core, workspaces, std::move(hooks)))
{
}

ProjectLayoutStore::~ProjectLayoutStore()
{
    *m_impl->alive = false;
}

QString ProjectLayoutStore::layoutFilePath(const QString& projectFolder)
{
    return QDir(projectFolder)
        .filePath(QString::fromLatin1(kProjectWorkspacesFolder) + QLatin1Char('/') +
                  QString::fromLatin1(kProjectLayoutFileName));
}

bool ProjectLayoutStore::save()
{
    Impl& impl = *m_impl;
    ProjectService& projects = impl.core.projects();
    // Nothing is written with no project open; an arrangement half restored is not the one to keep.
    if (!projects.isOpen() || impl.restoring)
    {
        return false;
    }
    const std::vector<ImageViewControl*> views = impl.core.viewService().getListOfViewControls();

    QSet<QString> listed;
    std::vector<ProjectWorkspace> workspaces;
    const QString current = impl.workspaces.current();
    for (const Workspace& workspace : impl.workspaces.workspaces())
    {
        ProjectWorkspace kept;
        kept.id = workspace.id;
        kept.name = workspace.name.left(kMaxWorkspaceNameLength);
        kept.locked = workspace.locked;
        kept.last = workspace.id == current;
        if (impl.hooks.dockTarget)
        {
            for (ImageViewControl* const view :
                 DockLayoutOperations::viewsDockedIn(impl.hooks.dockTarget(workspace.id), views))
            {
                if (std::optional<ProjectWorkspaceView> shown = impl.listedView(view, listed))
                {
                    kept.views.push_back(std::move(*shown));
                }
            }
        }
        workspaces.push_back(std::move(kept));
    }
    std::vector<ProjectWorkspaceView> floating;
    for (ImageViewControl* const view : views)
    {
        if (view != nullptr && view->isInFloatingWindow())
        {
            if (std::optional<ProjectWorkspaceView> shown = impl.listedView(view, listed))
            {
                floating.push_back(std::move(*shown));
            }
        }
    }

    // The layout first: a document that lists views with a layout that is older places them as far as it knows them.
    const QString layoutPath = layoutFilePath(projects.currentFolder());
    const QByteArray layout = DockHooks::saveLayout();
    if (layout.isEmpty())
    {
        // The library wrote none: the one kept is of another arrangement, and goes.
        LOG_WARNING("The arrangement of the project's workspaces could not be taken; its images are kept");
        (void)QFile::remove(layoutPath);
    }
    else if (!writeLayout(layoutPath, layout))
    {
        LOG_WARNING("The arrangement of the project's workspaces could not be written to '{}'",
                    QDir::toNativeSeparators(layoutPath).toStdString());
    }
    return projects.setWorkspaces(std::move(workspaces), std::move(floating));
}

bool ProjectLayoutStore::restore()
{
    Impl& impl = *m_impl;
    ProjectService& projects = impl.core.projects();
    const ProjectDocument* const document = projects.current();
    // One arrangement is restored at a time: the views a restore under way has asked for are its own to place.
    if (document == nullptr || document->workspaces.empty() || impl.restoring)
    {
        return false;
    }

    Run run;
    run.generation = ++impl.lastGeneration;
    run.folder = projects.currentFolder();
    run.workspaces = document->workspaces;
    run.layout = readLayout(layoutFilePath(run.folder));
    const std::vector<ProjectWorkspaceView> floating = document->floatingViews;
    for (const ProjectWorkspace& workspace : run.workspaces)
    {
        if (workspace.last)
        {
            run.lastWorkspace = workspace.id;
        }
    }

    // The shell's workspaces are the project's from here: a dock area is named by its workspace's id, which the
    // layout names it by.
    if (impl.hooks.setWorkspaces)
    {
        impl.hooks.setWorkspaces(run.workspaces);
    }

    // One view of each image the project lists, where its file is now.
    QSet<QString> listed;
    QStringList files;
    const auto list = [&](const ProjectWorkspaceView& view, const QString& workspaceId) {
        if (listed.contains(view.image))
        {
            return;
        }
        listed.insert(view.image);
        const ProjectImage* const image = projects.image(view.image);
        if (image == nullptr)
        {
            return;
        }
        const QString recorded = image->path;
        // The record follows a file that moved with the project.
        const QString path = cleaned(projects.locateImage(view.image));
        if (path.isEmpty())
        {
            run.missing.push_back(ProjectMissingImage{view.image, recorded});
            return;
        }
        run.pending.push_back(Pending{view.image, path, view.dockWidget, workspaceId, nullptr, false});
        files.append(path);
    };
    for (const ProjectWorkspace& workspace : run.workspaces)
    {
        for (const ProjectWorkspaceView& view : workspace.views)
        {
            list(view, workspace.id);
        }
    }
    for (const ProjectWorkspaceView& view : floating)
    {
        list(view, QString());
    }

    impl.run = std::move(run);
    impl.restoring = true;

    // The end comes later, on the GUI thread, whether there is a file to open or none.
    const std::uint64_t generation = impl.run.generation;
    const std::shared_ptr<bool> alive = impl.alive;
    Impl* const self = m_impl.get();
    std::function<void()> done = [alive, self, generation]() {
        if (*alive)
        {
            self->finish(generation);
        }
    };
    if (files.isEmpty() || !impl.hooks.openFiles)
    {
        QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(done), Qt::QueuedConnection);
    }
    else
    {
        impl.hooks.openFiles(files, std::move(done));
    }
    return true;
}

void ProjectLayoutStore::Impl::finish(const std::uint64_t generation)
{
    if (!restoring || generation != run.generation)
    {
        return;
    }
    Run ended = std::move(run);
    run = Run{};
    restoring = false;

    ProjectService& projects = core.projects();
    ProjectLayoutRestored restored;
    restored.missing = ended.missing;
    restored.lastWorkspace = ended.lastWorkspace;
    restored.projectStillOpen = projects.isOpen() && samePath(projects.currentFolder(), ended.folder);

    if (restored.projectStillOpen && !ended.layout.isEmpty())
    {
        // A name of the layout stands for the view opened for the image the project lists under it.
        const DockHooks::DockWidgetResolver resolver =
            [&ended](const QString& savedName) -> KDDockWidgets::QtWidgets::DockWidget* {
            for (const Pending& pending : ended.pending)
            {
                if (pending.dockWidget == savedName && !pending.view.isNull())
                {
                    return pending.view->getDockWidget();
                }
            }
            return nullptr;
        };
        restored.arranged = DockHooks::restoreLayout(ended.layout, resolver);
    }

    // A view the arrangement did not place is docked as a new view is: in its workspace while the project is the open
    // one, in the workspace on screen otherwise.
    for (const Pending& pending : ended.pending)
    {
        if (pending.view.isNull())
        {
            continue;
        }
        restored.views.emplace_back(pending.view);
        if (!isPlaced(*pending.view) && hooks.dockView)
        {
            hooks.dockView(*pending.view, restored.projectStillOpen ? pending.workspaceId : QString());
        }
    }
    if (restored.projectStillOpen && hooks.setLocked)
    {
        for (const ProjectWorkspace& workspace : ended.workspaces)
        {
            hooks.setLocked(workspace.id, workspace.locked);
        }
    }
    if (hooks.restored)
    {
        hooks.restored(restored);
    }
}

bool ProjectLayoutStore::isRestoring() const noexcept
{
    return m_impl->restoring;
}

bool ProjectLayoutStore::takes(ImageViewControl& view)
{
    Impl& impl = *m_impl;
    if (!impl.restoring)
    {
        return false;
    }
    const QString path = cleaned(QString::fromStdString(view.getFileName()));
    if (path.isEmpty())
    {
        return false;
    }
    for (Pending& pending : impl.run.pending)
    {
        if (!pending.arrived && samePath(pending.path, path))
        {
            pending.view = &view;
            pending.arrived = true;
            return true;
        }
    }
    return false;
}

std::vector<ImageViewControl*> ProjectLayoutStore::projectViews() const
{
    std::vector<ImageViewControl*> views;
    const ProjectService& projects = m_impl->core.projects();
    if (!projects.isOpen())
    {
        return views;
    }
    for (ImageViewControl* const view : m_impl->core.viewService().getListOfViewControls())
    {
        if (view == nullptr || view->getViewControlMode() != ViewControlMode::standard)
        {
            continue;
        }
        const QString image = m_impl->core.projectHistory().imageOf(*view);
        if (!image.isEmpty() && projects.image(image) != nullptr)
        {
            views.push_back(view);
        }
    }
    return views;
}

QString ProjectLayoutStore::lastWorkspace() const
{
    const Impl& impl = *m_impl;
    if (!impl.core.projects().isOpen())
    {
        return {};
    }
    // While the project's images open, the workspace it recorded; from then on, the one the person is in.
    if (impl.restoring && impl.workspaces.find(impl.run.lastWorkspace) != nullptr)
    {
        return impl.run.lastWorkspace;
    }
    return impl.workspaces.current();
}

QString ProjectLayoutStore::workspaceOf(const QString& imageId) const
{
    const ProjectDocument* const document = m_impl->core.projects().current();
    if (document == nullptr)
    {
        return {};
    }
    for (const ProjectWorkspace& workspace : document->workspaces)
    {
        const bool lists = std::any_of(workspace.views.begin(), workspace.views.end(),
                                       [&imageId](const ProjectWorkspaceView& view) { return view.image == imageId; });
        if (lists)
        {
            return workspace.id;
        }
    }
    return {};
}

} // namespace an::ui::shell
