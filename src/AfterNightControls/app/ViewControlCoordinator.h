// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <atomic>
#include <functional>
#include <map>
#include <string>

#include <QComboBox>
#include <QString>

class ImageViewControl;
class ImageViewControlManager;
class LogViewerControl;
class MainDashboard;
class QComboBox;
class QRecursiveMutex;

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

class AFTERNIGHTCONTROLS_API ViewControlCoordinator
{
public:
    struct Dependencies
    {
        ImageViewControlManager* imageViewControlManager = nullptr;
        QRecursiveMutex* imageViewControlManagerLock = nullptr;
        QComboBox* imageViewControlListComboBox = nullptr;
        bool* isTerminating = nullptr;
        LogViewerControl** logViewerControl = nullptr;
        MainDashboard** mainDashboard = nullptr;

        std::function<void()> updateActiveImageActions;
        std::function<void()> updateActiveImageHistory;
        std::function<void()> updateImageInfoInStatusBar;
        std::function<void(ImageViewControl*)> notifyActiveViewControlChanged;
        std::function<void()> switchToMainDashboard;
        std::function<void(const ImageViewControl*)> notifyViewControlDestroyed;
        std::function<void(const std::string&, const std::string&, const ImageViewControl*)> notifyViewControlRenamed;
        std::function<std::map<ImageViewControl*, KDDockWidgets::QtWidgets::DockWidget*>()>
            getDockWidgetsByViewControlMap;
    };

    explicit ViewControlCoordinator(const Dependencies& dependencies);

    void setActiveImageViewControl(ImageViewControl* view) const;
    void setActiveImageViewControlByName(const std::string& name,
                                         const std::function<void(ImageViewControl*)>& setActiveImageViewControl) const;
    void setActiveImageViewControlByNameAsync(
        const std::string& name,
        const std::function<void(ImageViewControl*, std::atomic_bool*)>& setActiveImageViewControlAsync,
        std::atomic_bool* finished) const;

    void addImageViewControl(ImageViewControl* view) const;
    void removeImageViewControl(ImageViewControl* view, bool deleteObject) const;
    void renameImageViewControl(ImageViewControl* view) const;
    void updateImageViewControlList() const;

private:
    Dependencies m_dependencies;
};

} // namespace an::ui
