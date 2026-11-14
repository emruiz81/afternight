// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "ViewControlCoordinator.h"

#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/KDDockWidgetsHelper.h"
#include "controls/ImageSelectorTooltipBuilder.h"

#include <QComboBox>

namespace an::ui
{

ViewControlCoordinator::ViewControlCoordinator(const Dependencies& dependencies) : m_dependencies(dependencies) {}

void ViewControlCoordinator::viewActivated(ImageViewControl* view) const
{
    if (!view || !m_dependencies.imageViewControlListComboBox)
    {
        return;
    }

    const QString targetSelection = QString::fromUtf8(view->getUniqueName());
    const QString currentSelection = m_dependencies.imageViewControlListComboBox->currentText();
    if (shouldUpdateImageSelectorSelection(currentSelection, targetSelection))
    {
        syncActiveImageSelectorSelection(m_dependencies.imageViewControlListComboBox, targetSelection);
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

void ViewControlCoordinator::viewAdded(ImageViewControl* view) const
{
    if (!view || !m_dependencies.imageViewControlListComboBox)
    {
        return;
    }

    syncRegisteredImageSelectorEntry(m_dependencies.imageViewControlListComboBox,
                                     QString::fromUtf8(view->getUniqueName()), an::ui::buildImageSelectorTooltip(view));
}

void ViewControlCoordinator::viewRemoved(ImageViewControl* view) const
{
    if (!view || !m_dependencies.imageViewControlListComboBox)
    {
        return;
    }

    const int itemId = m_dependencies.imageViewControlListComboBox->findText(QString::fromUtf8(view->getUniqueName()));
    if (itemId == -1)
    {
        return;
    }

    m_dependencies.imageViewControlListComboBox->removeItem(itemId);
    const bool hasOpenedImages = m_dependencies.imageViewControlListComboBox->count() > 0;
    m_dependencies.imageViewControlListComboBox->setEnabled(hasOpenedImages);
}

void ViewControlCoordinator::viewRenamed(ImageViewControl* view, const std::string& previousName) const
{
    if (!view || !m_dependencies.imageViewControlListComboBox)
    {
        return;
    }

    const int itemId = m_dependencies.imageViewControlListComboBox->findText(QString::fromUtf8(previousName));
    if (itemId != -1)
    {
        m_dependencies.imageViewControlListComboBox->removeItem(itemId);
    }

    m_dependencies.imageViewControlListComboBox->addItem(QString::fromUtf8(view->getUniqueName()));
    const int newItemId = m_dependencies.imageViewControlListComboBox->count() - 1;
    m_dependencies.imageViewControlListComboBox->setItemData(newItemId, an::ui::buildImageSelectorTooltip(view),
                                                             Qt::ToolTipRole);
}

void ViewControlCoordinator::viewUpdated(const ImageViewControl* view) const
{
    if (!view || !m_dependencies.imageViewControlListComboBox)
    {
        return;
    }

    const int itemId = m_dependencies.imageViewControlListComboBox->findText(QString::fromUtf8(view->getUniqueName()));
    if (itemId >= 0)
    {
        m_dependencies.imageViewControlListComboBox->setItemData(itemId, an::ui::buildImageSelectorTooltip(view),
                                                                 Qt::ToolTipRole);
    }
}

void ViewControlCoordinator::updateImageViewControlList() const
{
    if (!m_dependencies.imageViewControlManager || !m_dependencies.imageViewControlListComboBox)
    {
        return;
    }

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
