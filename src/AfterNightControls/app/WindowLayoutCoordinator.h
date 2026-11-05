// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <functional>
#include <list>
#include <map>
#include <string>
#include <utility>

#include <QIcon>
#include <QString>

namespace an::ui::app
{
class AfterNightAppUiElements;
}

class ImageViewControl;
class ImageViewControlManager;
class MainDashboard;
class QComboBox;
class QContextMenuEvent;
class QObject;
class QScrollArea;
class QToolBar;
class QToolButton;

namespace KDDockWidgets::QtWidgets
{
class DockWidget;
class MainWindow;
} // namespace KDDockWidgets::QtWidgets

namespace an::ui
{

inline bool shouldDisableWindowLayoutActions(const bool viewIsMaximized)
{
    return viewIsMaximized;
}

inline std::pair<std::string, QString> maximizeActionPresentation(const bool maximized)
{
    if (maximized)
    {
        return {"fullscreen_exit", QStringLiteral("Restore View")};
    }

    return {"fullscreen", QStringLiteral("Maximize View")};
}

class AFTERNIGHTCONTROLS_API WindowLayoutCoordinator
{
public:
    struct Dependencies
    {
        an::ui::app::AfterNightAppUiElements* ui = nullptr;
        ImageViewControlManager* imageViewControlManager = nullptr;
        QComboBox* imageViewControlListComboBox = nullptr;
        QObject* timerContext = nullptr;

        MainDashboard** mainDashboard = nullptr;
        QScrollArea** mainDashboardScrollArea = nullptr;
        KDDockWidgets::QtWidgets::MainWindow** dockMainWindow = nullptr;
        QToolBar** dockManagerToolBar = nullptr;
        QToolButton** toolbarLockLayout = nullptr;
        bool* unlockLayoutWhenRestoring = nullptr;

        std::function<QIcon(const std::string&)> iconProvider;
        std::function<void(ImageViewControl*)> setActiveImageViewControl;
        std::function<void(QToolBar*)> addToolBar;
        std::function<void()> toggleLayout;
        std::function<void()> lockLayout;
        std::function<void()> unlockLayout;
        std::function<bool()> layoutIsLocked;
    };

    explicit WindowLayoutCoordinator(const Dependencies& dependencies);

    void createContextMenuForViewControl(ImageViewControl* view, QContextMenuEvent* event) const;

    void switchToMainDashboard() const;
    void switchToDockManager() const;

    void createAuxToolBars() const;

    void splitAllImages(ImageViewControl* currentViewControl) const;
    void groupAllImages(ImageViewControl* currentViewControl) const;
    void fitAllImagesToWindow(ImageViewControl* currentViewControl) const;

    void splitAllImages() const;
    void groupAllImages() const;

    bool viewControlInAMaximizedContainer(ImageViewControl* control) const;
    ImageViewControl* getViewControlInAMaximizedContainer(ImageViewControl* control) const;

    std::map<KDDockWidgets::QtWidgets::DockWidget*, ImageViewControl*> getViewControlsByDockWidgetMap() const;
    std::map<ImageViewControl*, KDDockWidgets::QtWidgets::DockWidget*> getDockWidgetsByViewControlMap() const;

    std::list<ImageViewControl*> getListOfMaximizedViewControls() const;
    std::list<ImageViewControl*> getListOfViewControls() const;
    std::list<ImageViewControl*> getListOfViewControlsSharingTheSameWindow(ImageViewControl* currentViewControl) const;

    void maximizeViewControl(ImageViewControl* ctrl) const;
    void restoreViewControl(ImageViewControl* ctrl) const;

private:
    Dependencies m_dependencies;
};

} // namespace an::ui
