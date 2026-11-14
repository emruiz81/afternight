// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <functional>
#include <map>
#include <string>

#include <QComboBox>
#include <QString>

class ImageViewControl;
class ImageViewControlManager;
class MainDashboard;
class QComboBox;

namespace KDDockWidgets::QtWidgets
{
class DockWidget;
}

namespace an::ui
{

inline bool shouldUpdateImageSelectorSelection(const QString& currentSelection, const QString& targetSelection)
{
    return currentSelection != targetSelection;
}

inline bool shouldSwitchToMainDashboardAfterListRefresh(const bool imageListEmpty, const bool hasMainDashboard)
{
    return imageListEmpty && !hasMainDashboard;
}

inline bool shouldPromoteActiveDockWidget(const bool hasDockWidget, const bool dockWidgetInMainWindow,
                                          const bool dockWidgetInFloatingWindow)
{
    return hasDockWidget && (dockWidgetInMainWindow || dockWidgetInFloatingWindow);
}

inline void syncRegisteredImageSelectorEntry(QComboBox* imageViewControlListComboBox, const QString& viewName,
                                             const QString& toolTip)
{
    if (!imageViewControlListComboBox)
    {
        return;
    }

    const bool signalsWereBlocked = imageViewControlListComboBox->blockSignals(true);
    imageViewControlListComboBox->addItem(viewName);
    const int itemId = imageViewControlListComboBox->count() - 1;
    imageViewControlListComboBox->setItemData(itemId, toolTip, Qt::ToolTipRole);
    imageViewControlListComboBox->setCurrentText(viewName);
    imageViewControlListComboBox->blockSignals(signalsWereBlocked);
}

inline void syncActiveImageSelectorSelection(QComboBox* imageViewControlListComboBox, const QString& viewName)
{
    if (!imageViewControlListComboBox)
    {
        return;
    }

    const bool signalsWereBlocked = imageViewControlListComboBox->blockSignals(true);
    imageViewControlListComboBox->setCurrentText(viewName);
    imageViewControlListComboBox->blockSignals(signalsWereBlocked);
}

/// The classic window's half of its views: the image selector that lists them, and how it brings the active one
/// forward among the docks. The views' list is the application's; the window is told of each change to it and shows it.
class AFTERNIGHTCONTROLS_API ViewControlCoordinator
{
public:
    struct Dependencies
    {
        /// The application's list of views, read here and never changed.
        ImageViewControlManager* imageViewControlManager = nullptr;
        QComboBox* imageViewControlListComboBox = nullptr;
        MainDashboard** mainDashboard = nullptr;

        std::function<void()> switchToMainDashboard;
        std::function<std::map<ImageViewControl*, KDDockWidgets::QtWidgets::DockWidget*>()>
            getDockWidgetsByViewControlMap;
    };

    explicit ViewControlCoordinator(const Dependencies& dependencies);

    /// Selects the view in the selector, brings its dock forward and marks its tab group the active one.
    void viewActivated(ImageViewControl* view) const;

    /// Lists a view that joined the application's list, and selects it.
    void viewAdded(ImageViewControl* view) const;

    /// Takes a view that left the application's list off the selector; the selector is disabled once it lists none.
    void viewRemoved(ImageViewControl* view) const;

    /// Lists a renamed view under its new name.
    void viewRenamed(ImageViewControl* view, const std::string& previousName) const;

    /// Restates a view's tooltip after it changed.
    void viewUpdated(const ImageViewControl* view) const;

    /// Lists the application's views again, and shows the dashboard when there are none and none is shown.
    void updateImageViewControlList() const;

private:
    Dependencies m_dependencies;
};

} // namespace an::ui
