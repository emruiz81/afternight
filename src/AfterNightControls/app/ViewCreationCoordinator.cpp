// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ViewCreationCoordinator.h"

#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "session/ImageDocument.h"

#include <QPointer>
#include <QScrollArea>
#include <QTimer>
#include <QWidget>

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/MainWindow.h>

namespace
{

constexpr int kDockLayoutSettledFitDelayMs = 100;

bool isDockWidgetAttached(ImageViewControl* imageViewControl)
{
    if (!imageViewControl)
    {
        return false;
    }

    auto* dockWidget = imageViewControl->getDockWidget();
    auto* dockController = dockWidget ? dockWidget->dockWidget() : nullptr;
    return dockController && (dockController->isInMainWindow() || dockController->floatingWindow() != nullptr);
}

void applyPendingChanges(ImageViewControl* imageViewControl, const bool pendingChanges)
{
    if (!imageViewControl)
    {
        return;
    }

    if (pendingChanges)
    {
        imageViewControl->markPendingChanges();
    }
    else
    {
        imageViewControl->resetPendingChanges();
    }
}

void ensureDockManagerVisible(const an::ui::ViewCreationCoordinator::Dependencies& dependencies)
{
    auto* dockMainWindow = dependencies.dockMainWindow ? *dependencies.dockMainWindow : nullptr;
    const QWidget* currentWidget = dependencies.currentCentralWidget ? dependencies.currentCentralWidget() : nullptr;

    if (!dockMainWindow || currentWidget == dockMainWindow || !dependencies.switchToDockManager)
    {
        return;
    }
    dependencies.switchToDockManager();
}

/// Gives a view made in the standard mode its icon and its place: through the placement the coordinator was given, or
/// docked beside its neighbour or on the right, and the dock area shown.
void placeCreatedView(const an::ui::ViewCreationCoordinator::Dependencies& dependencies,
                      ImageViewControl& imageViewControl, ImageViewControl* neighbour, const bool keepFocusedNeighbour,
                      const bool fromSequence)
{
    auto* dockWidget = imageViewControl.getDockWidget();
    if (dockWidget && dependencies.iconProvider)
    {
        dockWidget->setIcon(dependencies.iconProvider(an::ui::iconNameForCreatedView(fromSequence)));
    }

    if (dependencies.placeView)
    {
        dependencies.placeView(imageViewControl, neighbour, keepFocusedNeighbour);
        return;
    }

    if (dockWidget)
    {
        an::ui::dockCreatedView(imageViewControl, neighbour,
                                dependencies.dockMainWindow ? *dependencies.dockMainWindow : nullptr);
        ensureDockManagerVisible(dependencies);
    }
}

void scheduleImageFitAfterDockLayoutSettles(ImageViewControl* imageViewControl,
                                            const QPointer<ImageViewControl>& viewGuard)
{
    if (!imageViewControl)
    {
        return;
    }

    QTimer::singleShot(0, imageViewControl, [viewGuard]() {
        if (!viewGuard)
        {
            return;
        }

        viewGuard->adjustImageToWindow();
    });

    // KDDockWidgets can post an additional layout pass after inserting a dock,
    // especially when several workflow outputs are opened in quick succession.
    QTimer::singleShot(kDockLayoutSettledFitDelayMs, imageViewControl, [viewGuard]() {
        if (!viewGuard)
        {
            return;
        }

        viewGuard->adjustImageToWindow();
    });
}

void showAndAdjustToWindow(ImageViewControl* imageViewControl, const bool adjustImageToWindow = true)
{
    if (!imageViewControl)
    {
        return;
    }

    imageViewControl->show();

    QPointer<ImageViewControl> viewGuard(imageViewControl);
    QTimer::singleShot(0, imageViewControl, [viewGuard]() {
        if (!viewGuard)
        {
            return;
        }

        if (isDockWidgetAttached(viewGuard))
        {
            viewGuard->setActiveControl();
        }
        else
        {
            viewGuard->show();
        }
    });

    if (adjustImageToWindow)
    {
        scheduleImageFitAfterDockLayoutSettles(imageViewControl, viewGuard);
    }
}

void propagateLineage(ImageViewControl* sourceControl, ImageViewControl* newControl)
{
    if (!sourceControl || !newControl)
    {
        return;
    }

    // The new view's history continues the lineage of the view its image was made from: that history's own lineage
    // and every step it recorded.
    an::ui::IViewHistory* source = sourceControl->viewHistory();
    an::ui::IViewHistory* destination = newControl->viewHistory();
    if (source != nullptr && destination != nullptr)
    {
        destination->inheritLineage(*source);
    }
}

} // anonymous namespace

namespace an::ui
{

void dockCreatedView(ImageViewControl& view, ImageViewControl* neighbour,
                     KDDockWidgets::QtWidgets::MainWindow* dockMainWindow)
{
    auto* dockWidget = view.getDockWidget();
    if (!dockWidget)
    {
        return;
    }

    if (neighbour && neighbour->getDockWidget())
    {
        neighbour->getDockWidget()->addDockWidgetAsTab(dockWidget);
    }
    else if (dockMainWindow)
    {
        dockMainWindow->addDockWidget(dockWidget, KDDockWidgets::Location_OnRight);
    }
}

ViewCreationCoordinator::ViewCreationCoordinator(const Dependencies& dependencies) : m_dependencies(dependencies) {}

ImageViewControl* ViewCreationCoordinator::createNewImageViewControl(const std::string& name, QWidget* parent,
                                                                     const ViewControlMode mode,
                                                                     const bool pendingChanges) const
{
    if (!m_dependencies.imageViewControlManager)
    {
        return nullptr;
    }

    if (mode == ViewControlMode::standard)
    {
        const QWidget* currentWidget =
            m_dependencies.currentCentralWidget ? m_dependencies.currentCentralWidget() : nullptr;
        QScrollArea* mainDashboard =
            m_dependencies.mainDashboardScrollArea ? *m_dependencies.mainDashboardScrollArea : nullptr;
        auto* dockMainWindow = m_dependencies.dockMainWindow ? *m_dependencies.dockMainWindow : nullptr;

        ImageViewControl* activeControl = nullptr;
        if (currentWidget && currentWidget != mainDashboard && currentWidget != dockMainWindow)
        {
            activeControl = m_dependencies.imageViewControlManager->getActiveImageViewControl();
        }

        const NewViewLayoutTransitionDecision decision = decideNewViewLayoutTransition(
            true, currentWidget == mainDashboard, currentWidget == dockMainWindow, activeControl != nullptr,
            activeControl ? activeControl->isInFloatingWindow() : false,
            activeControl ? activeControl->isMaximized() : false);

        if (decision.shouldRestoreMaximizedActiveView && activeControl)
        {
            activeControl->switchMaximized();
        }

        if (decision.shouldSwitchToDockManager && m_dependencies.switchToDockManager)
        {
            m_dependencies.switchToDockManager();
        }
    }

    auto* imageViewControl = new ImageViewControl(parent);

    std::string uniqueName = m_dependencies.imageViewControlManager->createImageViewControlUniqueName(name);

    imageViewControl->initialize(uniqueName, mode);
    applyPendingChanges(imageViewControl, pendingChanges);

    return imageViewControl;
}

ImageViewControl* ViewCreationCoordinator::createNewImageViewControlFromImage(
    ImageViewControl* customViewControl, ImageDocument&& document, QWidget* parent, const std::string& historyDesc,
    const ViewControlMode mode, ImageViewControl* neighbour, const bool keepFocusedNeighbour,
    const bool pendingChanges) const
{
    // A document no view is made for is left with the caller as it was handed in.
    if (!m_dependencies.imageViewControlManager || !document.valid() || !document.isOpen())
    {
        return nullptr;
    }

    ImageViewControl* imageViewControl = customViewControl;
    if (!imageViewControl)
    {
        imageViewControl = createNewImageViewControl(document.names().displayName.toStdString(), parent, mode, true);
    }

    if (!imageViewControl)
    {
        return nullptr;
    }

    imageViewControl->setDocument(std::move(document));

    imageViewControl->initializeNewImageHistory(resolveCreatedViewHistoryDescription(historyDesc, false));

    propagateLineage(neighbour, imageViewControl);

    if (mode == ViewControlMode::standard)
    {
        placeCreatedView(m_dependencies, *imageViewControl, neighbour, keepFocusedNeighbour, false);
    }

    std::string prefix;
    if (mode == ViewControlMode::rt_preview)
    {
        prefix = "** RT PREVIEW **";
    }

    imageViewControl->setWindowTitle(QString::fromUtf8(prefix + imageViewControl->getUniqueName()));
    imageViewControl->displayDefaultViewChannel();
    if (mode != ViewControlMode::rt_preview)
    {
        showAndAdjustToWindow(imageViewControl, true);
    }
    applyPendingChanges(imageViewControl, pendingChanges);

    if (m_dependencies.updateActiveImageActions)
    {
        m_dependencies.updateActiveImageActions();
    }
    if (m_dependencies.updateActiveImageHistory)
    {
        m_dependencies.updateActiveImageHistory();
    }
    if (m_dependencies.updateImageInfoInStatusBar)
    {
        m_dependencies.updateImageInfoInStatusBar();
    }

    if (mode == ViewControlMode::standard && m_dependencies.notifyViewControlCreated)
    {
        m_dependencies.notifyViewControlCreated(imageViewControl);
    }

    return imageViewControl;
}

ImageViewControl* ViewCreationCoordinator::createNewImageViewControlFromSequence(
    ImageViewControl* customViewControl, ImageDocument&& document, QWidget* parent, const bool autoDebayer,
    const std::string& historyDesc, const ViewControlMode mode, ImageViewControl* neighbour,
    const bool keepFocusedNeighbour, const bool pendingChanges) const
{
    // A document that states no sequence makes no view, and stays the caller's.
    if (!m_dependencies.imageViewControlManager || !document.valid() || !document.isOpen() ||
        !document.sequence().has_value())
    {
        return nullptr;
    }

    ImageViewControl* imageViewControl = customViewControl;
    if (!imageViewControl)
    {
        imageViewControl = createNewImageViewControl(document.names().displayName.toStdString(), parent, mode, true);
        if (!imageViewControl)
        {
            return nullptr;
        }
    }

    imageViewControl->setSequenceDocument(std::move(document), autoDebayer);

    imageViewControl->initializeNewImageHistory(resolveCreatedViewHistoryDescription(historyDesc, true));

    propagateLineage(neighbour, imageViewControl);

    if (mode == ViewControlMode::standard)
    {
        placeCreatedView(m_dependencies, *imageViewControl, neighbour, keepFocusedNeighbour, true);
    }

    imageViewControl->setWindowTitle(QString::fromUtf8(imageViewControl->getUniqueName()));
    imageViewControl->displayDefaultViewChannel();
    showAndAdjustToWindow(imageViewControl);
    applyPendingChanges(imageViewControl, pendingChanges);

    if (m_dependencies.updateActiveImageActions)
    {
        m_dependencies.updateActiveImageActions();
    }
    if (m_dependencies.updateActiveImageHistory)
    {
        m_dependencies.updateActiveImageHistory();
    }
    if (m_dependencies.updateImageInfoInStatusBar)
    {
        m_dependencies.updateImageInfoInStatusBar();
    }

    if (mode == ViewControlMode::standard && m_dependencies.notifyViewControlCreated)
    {
        m_dependencies.notifyViewControlCreated(imageViewControl);
    }

    return imageViewControl;
}

} // namespace an::ui
