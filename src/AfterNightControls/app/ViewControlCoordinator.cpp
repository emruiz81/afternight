// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ViewControlCoordinator.h"

#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/KDDockWidgetsHelper.h"
#include "controls/ImageSelectorTooltipBuilder.h"

#include <QComboBox>
#include <QMutexLocker>

#include <AfterNightPlatform/Logging.h>

namespace an::ui
{

ViewControlCoordinator::ViewControlCoordinator(const Dependencies& dependencies) : m_dependencies(dependencies) {}

void ViewControlCoordinator::setActiveImageViewControl(ImageViewControl* view) const
{
    if (!view || !m_dependencies.imageViewControlManager || !m_dependencies.imageViewControlManagerLock ||
        !m_dependencies.imageViewControlListComboBox)
    {
        return;
    }

    QMutexLocker lock(m_dependencies.imageViewControlManagerLock);

    m_dependencies.imageViewControlManager->setActiveImageViewControl(view);

    const QString targetSelection = QString::fromUtf8(view->getUniqueName());
    const QString currentSelection = m_dependencies.imageViewControlListComboBox->currentText();
    if (shouldUpdateImageSelectorSelection(currentSelection, targetSelection))
    {
        syncActiveImageSelectorSelection(m_dependencies.imageViewControlListComboBox, targetSelection);
    }

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
    if (m_dependencies.notifyActiveViewControlChanged)
    {
        m_dependencies.notifyActiveViewControlChanged(view);
    }

    auto* dockWidget = view->getDockWidget();
    auto* dockController = dockWidget ? dockWidget->dockWidget() : nullptr;
    if (shouldPromoteActiveDockWidget(dockWidget != nullptr, dockController && dockController->isInMainWindow(),
                                      dockController && dockController->floatingWindow() != nullptr))
    {
        dockWidget->show();
        dockWidget->setAsCurrentTab();
        view->show();
        view->raise();
    }

    const AN_Stack* activeStack = view->getStack();
    const auto map = m_dependencies.getDockWidgetsByViewControlMap
                         ? m_dependencies.getDockWidgetsByViewControlMap()
                         : std::map<ImageViewControl*, KDDockWidgets::QtWidgets::DockWidget*>();

    for (const auto& [otherView, _] : map)
    {
        (void)_;
        if (!otherView)
        {
            continue;
        }

        AN_TabBar* tabBar = otherView->getTabBar();
        AN_Stack* stack = otherView->getStack();
        if (!tabBar || !stack)
        {
            continue;
        }

        const bool isActiveGroup = activeStack && stack == activeStack;
        tabBar->setForceUnselected(!isActiveGroup);
    }
}

void ViewControlCoordinator::setActiveImageViewControlByName(
    const std::string& name, const std::function<void(ImageViewControl*)>& setActiveImageViewControl) const
{
    if (!m_dependencies.imageViewControlManager || !setActiveImageViewControl)
    {
        return;
    }

    if (auto* viewControl = m_dependencies.imageViewControlManager->getImageViewControlByName(name))
    {
        setActiveImageViewControl(viewControl);
    }
}

void ViewControlCoordinator::setActiveImageViewControlByNameAsync(
    const std::string& name,
    const std::function<void(ImageViewControl*, std::atomic_bool*)>& setActiveImageViewControlAsync,
    std::atomic_bool* finished) const
{
    if (!m_dependencies.imageViewControlManager || !setActiveImageViewControlAsync)
    {
        return;
    }

    if (auto* viewControl = m_dependencies.imageViewControlManager->getImageViewControlByName(name))
    {
        setActiveImageViewControlAsync(viewControl, finished);
    }
}

void ViewControlCoordinator::addImageViewControl(ImageViewControl* view) const
{
    if (!view || !m_dependencies.imageViewControlManager || !m_dependencies.imageViewControlManagerLock ||
        !m_dependencies.imageViewControlListComboBox)
    {
        return;
    }

    QMutexLocker lock(m_dependencies.imageViewControlManagerLock);

    m_dependencies.imageViewControlManager->registerImageViewControl(view);
    syncRegisteredImageSelectorEntry(m_dependencies.imageViewControlListComboBox,
                                     QString::fromUtf8(view->getUniqueName()), an::ui::buildImageSelectorTooltip(view));

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
    if (m_dependencies.notifyActiveViewControlChanged)
    {
        m_dependencies.notifyActiveViewControlChanged(view);
    }
}

void ViewControlCoordinator::removeImageViewControl(ImageViewControl* view, const bool deleteObject) const
{
    if (!view || !m_dependencies.imageViewControlManager || !m_dependencies.imageViewControlManagerLock ||
        !m_dependencies.imageViewControlListComboBox)
    {
        return;
    }

    QMutexLocker lock(m_dependencies.imageViewControlManagerLock);

    m_dependencies.imageViewControlManager->unregisterImageViewControl(view);

    const QString viewName = QString::fromUtf8(view->getUniqueName());
    const int itemId = m_dependencies.imageViewControlListComboBox->findText(viewName);

    if (itemId != -1)
    {
        if (m_dependencies.notifyViewControlDestroyed)
        {
            m_dependencies.notifyViewControlDestroyed(view);
        }

        m_dependencies.imageViewControlListComboBox->removeItem(itemId);

        const bool hasOpenedImages = m_dependencies.imageViewControlListComboBox->count() > 0;
        m_dependencies.imageViewControlListComboBox->setEnabled(hasOpenedImages);

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
        if (m_dependencies.notifyActiveViewControlChanged)
        {
            m_dependencies.notifyActiveViewControlChanged(
                m_dependencies.imageViewControlManager->getActiveImageViewControl());
        }

        const bool terminating = m_dependencies.isTerminating ? *m_dependencies.isTerminating : false;
        LOG_INFO_IF(!terminating, "Image '{}' closed", view->getUniqueName());
    }

    if (m_dependencies.imageViewControlManager->getNumOfImageViewControls() == 0 && m_dependencies.logViewerControl &&
        *m_dependencies.logViewerControl == nullptr && m_dependencies.switchToMainDashboard)
    {
        m_dependencies.switchToMainDashboard();
    }

    if (deleteObject)
    {
        delete view;
    }
}

void ViewControlCoordinator::renameImageViewControl(ImageViewControl* view) const
{
    if (!view || !m_dependencies.imageViewControlManager || !m_dependencies.imageViewControlManagerLock ||
        !m_dependencies.imageViewControlListComboBox)
    {
        return;
    }

    QMutexLocker lock(m_dependencies.imageViewControlManagerLock);

    const auto oldName = m_dependencies.imageViewControlManager->getImageViewControlNameByAddr(view);
    m_dependencies.imageViewControlManager->renameRegisteredImageViewControl(view);

    const auto newName = view->getUniqueName();
    const QString newNameQt = QString::fromUtf8(newName);
    const QString oldNameQt = QString::fromUtf8(oldName);

    const int itemId = m_dependencies.imageViewControlListComboBox->findText(oldNameQt);
    if (itemId != -1)
    {
        m_dependencies.imageViewControlListComboBox->removeItem(itemId);
    }

    m_dependencies.imageViewControlListComboBox->addItem(newNameQt);
    {
        const int newItemId = m_dependencies.imageViewControlListComboBox->count() - 1;
        m_dependencies.imageViewControlListComboBox->setItemData(newItemId, an::ui::buildImageSelectorTooltip(view),
                                                                 Qt::ToolTipRole);
    }

    LOG_INFO("Image '{}' renamed to '{}'", oldName, newName);
    if (m_dependencies.notifyViewControlRenamed)
    {
        m_dependencies.notifyViewControlRenamed(newName, oldName, view);
    }
}

void ViewControlCoordinator::updateImageViewControlList() const
{
    if (!m_dependencies.imageViewControlManager || !m_dependencies.imageViewControlManagerLock ||
        !m_dependencies.imageViewControlListComboBox)
    {
        return;
    }

    QMutexLocker lock(m_dependencies.imageViewControlManagerLock);

    m_dependencies.imageViewControlListComboBox->blockSignals(true);
    m_dependencies.imageViewControlListComboBox->clear();

    const auto imageList = m_dependencies.imageViewControlManager->getListOfImageViewControlNames();
    m_dependencies.imageViewControlListComboBox->setEnabled(!imageList.empty());

    for (const auto& name : imageList)
    {
        m_dependencies.imageViewControlListComboBox->addItem(QString::fromUtf8(name));
        const int itemId = m_dependencies.imageViewControlListComboBox->count() - 1;
        if (auto* view = m_dependencies.imageViewControlManager->getImageViewControlByName(name))
        {
            m_dependencies.imageViewControlListComboBox->setItemData(itemId, an::ui::buildImageSelectorTooltip(view),
                                                                     Qt::ToolTipRole);
        }
    }

    const bool hasMainDashboard = m_dependencies.mainDashboard && *m_dependencies.mainDashboard;
    if (shouldSwitchToMainDashboardAfterListRefresh(imageList.empty(), hasMainDashboard) &&
        m_dependencies.switchToMainDashboard)
    {
        m_dependencies.switchToMainDashboard();
    }

    m_dependencies.imageViewControlListComboBox->blockSignals(false);

    if (auto* activeControl = m_dependencies.imageViewControlManager->getActiveImageViewControl())
    {
        syncActiveImageSelectorSelection(m_dependencies.imageViewControlListComboBox,
                                         QString::fromUtf8(activeControl->getUniqueName()));
    }
}

} // namespace an::ui
