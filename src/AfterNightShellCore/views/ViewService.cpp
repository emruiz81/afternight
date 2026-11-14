// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "views/ViewService.h"

#include "actions/ActionStateCoordinator.h"
#include "core/ApplicationCore.h"
#include "core/IShell.h"
#include "services/AppEventBus.h"
#include "services/ExtensionService.h"

#include "app/IconManager.h"
#include "app/ViewCreationCoordinator.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "session/ImageDocument.h"

#include <AfterNightPlatform/Logging.h>

#include <QMutexLocker>
#include <QPointer>
#include <QRecursiveMutex>

#include <optional>
#include <utility>

namespace an::ui::shell
{

namespace
{

/// Calls `hook` when it is set.
template <typename HookT, typename... ArgsT> void call(const HookT& hook, ArgsT&&... args)
{
    if (hook)
    {
        hook(std::forward<ArgsT>(args)...);
    }
}

/// States who asks for the views a factory call makes, for that call, and puts back what was stated before it.
class OriginScope
{
public:
    OriginScope(std::optional<ViewOrigin>& making, const ViewOrigin origin) : m_making(making), m_previous(making)
    {
        m_making = origin;
    }
    ~OriginScope()
    {
        m_making = m_previous;
    }
    OriginScope(const OriginScope&) = delete;
    OriginScope& operator=(const OriginScope&) = delete;

private:
    std::optional<ViewOrigin>& m_making;
    std::optional<ViewOrigin> m_previous;
};

} // namespace

struct ViewService::Impl
{
    /// The factory of the views the person asks for.
    class UserViewFactory final : public an::ui::IViewFactory
    {
    public:
        explicit UserViewFactory(Impl& impl) : m_impl(impl) {}

        [[nodiscard]] ImageViewControl*
        createNewImageViewControlFromImage(ImageViewControl* customViewControl, an::ui::ImageDocument&& document,
                                           QWidget* parent, const std::string& historyDesc, const ViewControlMode mode,
                                           ImageViewControl* neighbour, const bool keepFocusedNeighbour,
                                           const bool pendingChanges) override
        {
            const OriginScope scope(m_impl.making, ViewOrigin::User);
            return m_impl.creation->createNewImageViewControlFromImage(customViewControl, std::move(document), parent,
                                                                       historyDesc, mode, neighbour,
                                                                       keepFocusedNeighbour, pendingChanges);
        }

        [[nodiscard]] ImageViewControl*
        createNewImageViewControlFromSequence(ImageViewControl* customViewControl, an::ui::ImageDocument&& document,
                                              QWidget* parent, const bool autoDebayer, const std::string& historyDesc,
                                              const ViewControlMode mode, ImageViewControl* neighbour,
                                              const bool keepFocusedNeighbour, const bool pendingChanges) override
        {
            const OriginScope scope(m_impl.making, ViewOrigin::User);
            return m_impl.creation->createNewImageViewControlFromSequence(
                customViewControl, std::move(document), parent, autoDebayer, historyDesc, mode, neighbour,
                keepFocusedNeighbour, pendingChanges);
        }

    private:
        Impl& m_impl;
    };

    explicit Impl(ApplicationCore& owner) : core(owner), userViews(*this) {}

    [[nodiscard]] IViewHost* host() const
    {
        IShell* const shell = core.shell();
        return shell != nullptr ? &shell->viewHost() : nullptr;
    }

    /// The shell shows the active view's state, and the actions and the history follow the active view.
    void activeViewChanged(ImageViewControl* active)
    {
        core.actionState().updateActiveImageActions();
        core.actionState().updateActiveImageHistory();
        if (IShell* const shell = core.shell())
        {
            shell->statusDisplay().showImageInfo(manager.getActiveImageViewControl());
        }
        core.eventBus().notifyActiveViewControlChanged(active);
    }

    ApplicationCore& core;
    Hooks hooks;
    ImageViewControlManager manager;
    QRecursiveMutex lock;
    std::unique_ptr<an::ui::ViewCreationCoordinator> creation;
    UserViewFactory userViews;

    /// Who asks for the views the factory call under way makes; none outside a call.
    std::optional<ViewOrigin> making;

    /// The task's target and canvas a shell named, while it shows the task.
    bool taskSubjectSet = false;
    QPointer<ImageViewControl> taskTarget;
    QPointer<ImageViewControl> taskCanvas;
};

ViewService::ViewService(ApplicationCore& core) : m_impl(std::make_unique<Impl>(core))
{
    Impl& impl = *m_impl;
    an::ui::ViewCreationCoordinator::Dependencies creationDeps;
    creationDeps.imageViewControlManager = &impl.manager;
    creationDeps.iconProvider = [&impl](const std::string& iconName) {
        const IconManagerSptr& icons = impl.core.iconManager();
        return icons ? icons->getIcon(QString::fromUtf8(iconName)) : QIcon();
    };
    creationDeps.updateActiveImageActions = [&impl]() { impl.core.actionState().updateActiveImageActions(); };
    creationDeps.updateActiveImageHistory = [&impl]() { impl.core.actionState().updateActiveImageHistory(); };
    creationDeps.updateImageInfoInStatusBar = [&impl]() {
        if (IShell* const shell = impl.core.shell())
        {
            shell->statusDisplay().showImageInfo(impl.manager.getActiveImageViewControl());
        }
    };
    creationDeps.notifyViewControlCreated = [&impl](const ImageViewControl* view) {
        impl.core.extensions().viewCreated(view);
        impl.core.eventBus().notifyViewControlCreated(view);
    };
    creationDeps.placeView = [&impl](ImageViewControl& view, ImageViewControl* neighbour,
                                     const bool keepFocusedNeighbour) {
        if (IViewHost* const host = impl.host())
        {
            host->place(view, ViewPlacement{.neighbour = neighbour, .keepFocusedNeighbour = keepFocusedNeighbour});
        }
    };
    impl.creation = std::make_unique<an::ui::ViewCreationCoordinator>(creationDeps);
}

ViewService::~ViewService() = default;

void ViewService::setHooks(Hooks hooks)
{
    m_impl->hooks = std::move(hooks);
}

ImageViewControlManager& ViewService::manager() noexcept
{
    return m_impl->manager;
}

QRecursiveMutex& ViewService::managerLock() noexcept
{
    return m_impl->lock;
}

an::ui::IViewFactory& ViewService::userViews() noexcept
{
    return m_impl->userViews;
}

void ViewService::setActiveViewByName(const std::string& name)
{
    if (ImageViewControl* const view = m_impl->manager.getImageViewControlByName(name))
    {
        setActiveImageViewControl(view);
    }
}

void ViewService::updateActiveImageActions()
{
    m_impl->core.actionState().updateActiveImageActions();
}

void ViewService::updateActiveImageHistory()
{
    m_impl->core.actionState().updateActiveImageHistory();
}

void ViewService::refreshViewList()
{
    call(m_impl->hooks.refreshViewList);
}

ImageViewControl* ViewService::subjectView()
{
    if (m_impl->taskSubjectSet && m_impl->taskTarget)
    {
        return m_impl->taskTarget.data();
    }
    return m_impl->manager.getActiveImageViewControl();
}

ImageViewControl* ViewService::lookingView()
{
    if (m_impl->taskSubjectSet && m_impl->taskCanvas)
    {
        return m_impl->taskCanvas.data();
    }
    return m_impl->manager.getActiveImageViewControl();
}

void ViewService::setTaskSubject(ImageViewControl* target, ImageViewControl* canvas)
{
    m_impl->taskSubjectSet = true;
    m_impl->taskTarget = target;
    m_impl->taskCanvas = canvas;
}

void ViewService::clearTaskSubject()
{
    m_impl->taskSubjectSet = false;
    m_impl->taskTarget = nullptr;
    m_impl->taskCanvas = nullptr;
}

ImageViewControl*
ViewService::createNewImageViewControlFromImage(ImageViewControl* customViewControl, an::ui::ImageDocument&& document,
                                                QWidget* parent, const std::string& historyDesc,
                                                const ViewControlMode mode, ImageViewControl* neighbour,
                                                const bool keepFocusedNeighbour, const bool pendingChanges)
{
    const OriginScope scope(m_impl->making, ViewOrigin::Process);
    return m_impl->creation->createNewImageViewControlFromImage(customViewControl, std::move(document), parent,
                                                                historyDesc, mode, neighbour, keepFocusedNeighbour,
                                                                pendingChanges);
}

ImageViewControl* ViewService::createNewImageViewControlFromSequence(
    ImageViewControl* customViewControl, an::ui::ImageDocument&& document, QWidget* parent, const bool autoDebayer,
    const std::string& historyDesc, const ViewControlMode mode, ImageViewControl* neighbour,
    const bool keepFocusedNeighbour, const bool pendingChanges)
{
    const OriginScope scope(m_impl->making, ViewOrigin::Process);
    return m_impl->creation->createNewImageViewControlFromSequence(customViewControl, std::move(document), parent,
                                                                   autoDebayer, historyDesc, mode, neighbour,
                                                                   keepFocusedNeighbour, pendingChanges);
}

ImageViewControl* ViewService::getActiveImageViewControl()
{
    return m_impl->manager.getActiveImageViewControl();
}

void ViewService::setActiveImageViewControl(ImageViewControl* view)
{
    if (!view)
    {
        return;
    }

    QMutexLocker lock(&m_impl->lock);
    m_impl->manager.setActiveImageViewControl(view);
    if (IViewHost* const host = m_impl->host())
    {
        host->activate(*view);
    }
    m_impl->activeViewChanged(view);
}

void ViewService::addViewControl(ImageViewControl* view)
{
    if (!view)
    {
        return;
    }

    QMutexLocker lock(&m_impl->lock);
    m_impl->manager.registerImageViewControl(view);
    if (IViewHost* const host = m_impl->host())
    {
        const ViewOrigin origin = m_impl->making.value_or(ViewOrigin::Process);
        host->viewAdded(*view, origin);
        if (origin == ViewOrigin::User)
        {
            host->showViews();
        }
    }
    m_impl->activeViewChanged(view);
}

void ViewService::removeViewControl(ImageViewControl* view, const bool deleteObject)
{
    if (!view)
    {
        return;
    }

    QMutexLocker lock(&m_impl->lock);
    const bool listed = !m_impl->manager.getImageViewControlNameByAddr(view).empty();
    m_impl->manager.unregisterImageViewControl(view);
    if (m_impl->taskTarget == view || m_impl->taskCanvas == view)
    {
        clearTaskSubject();
    }

    IViewHost* const host = m_impl->host();
    if (listed)
    {
        m_impl->core.extensions().viewDestroyed(view);
        m_impl->core.eventBus().notifyViewControlDestroyed(view);
        if (host != nullptr)
        {
            host->viewRemoved(*view);
        }
        m_impl->activeViewChanged(m_impl->manager.getActiveImageViewControl());

        const bool terminating = m_impl->hooks.isTerminating && m_impl->hooks.isTerminating();
        LOG_INFO_IF(!terminating, "Image '{}' closed", view->getUniqueName());
    }

    if (m_impl->manager.getNumOfImageViewControls() == 0 && host != nullptr)
    {
        host->showHome();
    }

    if (deleteObject)
    {
        delete view;
    }
}

void ViewService::renameViewControl(ImageViewControl* view)
{
    if (!view)
    {
        return;
    }

    QMutexLocker lock(&m_impl->lock);
    const std::string oldName = m_impl->manager.getImageViewControlNameByAddr(view);
    m_impl->manager.renameRegisteredImageViewControl(view);
    const std::string newName = view->getUniqueName();
    if (IViewHost* const host = m_impl->host())
    {
        host->viewRenamed(*view, oldName);
    }

    LOG_INFO("Image '{}' renamed to '{}'", oldName, newName);
    m_impl->core.extensions().viewRenamed(newName, oldName, view);
    m_impl->core.eventBus().notifyViewControlRenamed(newName, oldName, view);
}

void ViewService::maximizeViewControl(ImageViewControl* ctrl)
{
    if (ctrl == nullptr)
    {
        return;
    }
    if (IViewHost* const host = m_impl->host())
    {
        host->maximize(*ctrl);
    }
}

void ViewService::restoreViewControl(ImageViewControl* ctrl)
{
    if (ctrl == nullptr)
    {
        return;
    }
    if (IViewHost* const host = m_impl->host())
    {
        host->restore(*ctrl);
    }
}

void ViewService::notifyViewControlUpdated(const ImageViewControl* ctrl)
{
    call(m_impl->hooks.viewUpdated, ctrl);
    m_impl->core.extensions().viewUpdated(ctrl);
    m_impl->core.eventBus().notifyViewControlUpdated(ctrl);
}

void ViewService::createContextMenuForViewControl(ImageViewControl* view, QContextMenuEvent* event)
{
    call(m_impl->hooks.contextMenu, view, event);
}

bool ViewService::supportsDockWidgets() const
{
    return true;
}

ImageViewControl* ViewService::getViewControlByName(const std::string& name)
{
    return m_impl->manager.getImageViewControlByName(name);
}

std::vector<ImageViewControl*> ViewService::getListOfViewControls()
{
    return m_impl->manager.getListOfImageViewControls();
}

std::string ViewService::createViewControlUniqueName(const std::string& name)
{
    return m_impl->manager.createImageViewControlUniqueName(name);
}

} // namespace an::ui::shell
