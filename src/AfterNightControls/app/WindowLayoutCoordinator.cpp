// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/WindowLayoutCoordinator.h"

#include "app/AfterNightAppUiBuilder.h"

#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/MainDashboard.h"

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>

#include <QComboBox>
#include <QScrollArea>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>


namespace an::ui
{

WindowLayoutCoordinator::WindowLayoutCoordinator(const Dependencies& dependencies) : m_dependencies(dependencies) {}

void WindowLayoutCoordinator::switchToMainDashboard() const
{
    if (!m_dependencies.ui || !m_dependencies.mainDashboardScrollArea)
    {
        return;
    }

    auto* mainDashboardScrollArea = *m_dependencies.mainDashboardScrollArea;
    if (!mainDashboardScrollArea)
    {
        return;
    }

    if (m_dependencies.ui->centralWidget->currentWidget() != mainDashboardScrollArea)
    {
        MainDashboard* mainDashboard =
            (m_dependencies.mainDashboard && *m_dependencies.mainDashboard) ? *m_dependencies.mainDashboard : nullptr;

        m_dependencies.ui->statusBar->setVisible(false);

        if (m_dependencies.dockManagerToolBar && *m_dependencies.dockManagerToolBar)
        {
            (*m_dependencies.dockManagerToolBar)->setEnabled(false);
        }

        m_dependencies.ui->menuWindow->setEnabled(false);

        m_dependencies.ui->actionLog_Viewer->setChecked(false);
        m_dependencies.ui->actionLog_Viewer->setEnabled(true);

        if (m_dependencies.imageViewControlListComboBox)
        {
            m_dependencies.imageViewControlListComboBox->setEnabled(false);
        }

        if (mainDashboard)
        {
            mainDashboard->reloadRecentFilesThumbnails();
        }

        m_dependencies.ui->centralWidget->setCurrentWidget(mainDashboardScrollArea);

        if (mainDashboard)
        {
            // Run one more refresh after the stacked-widget switch is applied
            // so the dashboard re-syncs recents against the final restored/maximized
            // geometry of the main window.
            QTimer::singleShot(0, mainDashboard, [mainDashboard]() {
                if (mainDashboard)
                {
                    mainDashboard->reloadRecentFilesThumbnails();
                }
            });
        }
    }
}

void WindowLayoutCoordinator::switchToDockManager() const
{
    if (!m_dependencies.ui || !m_dependencies.dockMainWindow)
    {
        return;
    }

    auto* dockMainWindow = *m_dependencies.dockMainWindow;
    if (!dockMainWindow)
    {
        return;
    }

    if (m_dependencies.ui->centralWidget->currentWidget() != dockMainWindow)
    {
        m_dependencies.ui->centralWidget->setCurrentWidget(dockMainWindow);

        m_dependencies.ui->statusBar->setVisible(true);

        if (m_dependencies.dockManagerToolBar && *m_dependencies.dockManagerToolBar)
        {
            (*m_dependencies.dockManagerToolBar)->setEnabled(true);
        }

        m_dependencies.ui->menuWindow->setEnabled(true);

        m_dependencies.ui->statusBar->setEnabled(true);
        if (m_dependencies.imageViewControlListComboBox)
        {
            m_dependencies.imageViewControlListComboBox->setEnabled(true);
        }

        m_dependencies.ui->actionLog_Viewer->setEnabled(true);
    }
}

void WindowLayoutCoordinator::createAuxToolBars() const
{
    if (!m_dependencies.ui || !m_dependencies.dockManagerToolBar || !m_dependencies.toolbarLockLayout ||
        !m_dependencies.addToolBar)
    {
        return;
    }

    if (*m_dependencies.dockManagerToolBar)
    {
        return;
    }

    auto* dockManagerToolBar = new QToolBar();

    auto* toolbarLockLayout = new QToolButton();
    QObject::connect(toolbarLockLayout, &QToolButton::clicked, m_dependencies.timerContext, [this]() {
        if (m_dependencies.toggleLayout)
        {
            m_dependencies.toggleLayout();
        }
    });

    const QIcon lockIcon = m_dependencies.iconProvider ? m_dependencies.iconProvider("lock") : QIcon();
    toolbarLockLayout->setIcon(lockIcon);
    toolbarLockLayout->setToolTip("Lock layout");

    dockManagerToolBar->addWidget(toolbarLockLayout);

    dockManagerToolBar->addAction(m_dependencies.ui->actionGroup_All_Images);
    dockManagerToolBar->addAction(m_dependencies.ui->actionSplit_All_Images);

    dockManagerToolBar->addSeparator();

    dockManagerToolBar->addAction(m_dependencies.ui->actionFit_Images_To_Window);

    dockManagerToolBar->setEnabled(false);

    m_dependencies.addToolBar(dockManagerToolBar);

    *m_dependencies.dockManagerToolBar = dockManagerToolBar;
    *m_dependencies.toolbarLockLayout = toolbarLockLayout;
}

bool WindowLayoutCoordinator::viewControlInAMaximizedContainer(ImageViewControl* control) const
{
    return getViewControlInAMaximizedContainer(control) != nullptr;
}

ImageViewControl* WindowLayoutCoordinator::getViewControlInAMaximizedContainer(ImageViewControl* control) const
{
    auto vControls = getListOfViewControlsSharingTheSameWindow(control);

    for (auto* vc : vControls)
    {
        if (vc && vc->isMaximized())
        {
            return vc;
        }
    }

    return nullptr;
}

std::map<KDDockWidgets::QtWidgets::DockWidget*, ImageViewControl*>
WindowLayoutCoordinator::getViewControlsByDockWidgetMap() const
{
    std::map<KDDockWidgets::QtWidgets::DockWidget*, ImageViewControl*> ret;

    if (!m_dependencies.imageViewControlManager)
    {
        return ret;
    }

    auto vControls = m_dependencies.imageViewControlManager->getListOfImageViewControls();
    for (auto* vc : vControls)
    {
        if (vc)
        {
            ret[vc->getDockWidget()] = vc;
        }
    }

    return ret;
}

std::map<ImageViewControl*, KDDockWidgets::QtWidgets::DockWidget*>
WindowLayoutCoordinator::getDockWidgetsByViewControlMap() const
{
    std::map<ImageViewControl*, KDDockWidgets::QtWidgets::DockWidget*> ret;

    if (!m_dependencies.imageViewControlManager)
    {
        return ret;
    }

    auto vControls = m_dependencies.imageViewControlManager->getListOfImageViewControls();
    for (auto* vc : vControls)
    {
        if (vc)
        {
            ret[vc] = vc->getDockWidget();
        }
    }

    return ret;
}

std::list<ImageViewControl*> WindowLayoutCoordinator::getListOfMaximizedViewControls() const
{
    std::list<ImageViewControl*> ret;

    if (!m_dependencies.imageViewControlManager)
    {
        return ret;
    }

    auto vControls = m_dependencies.imageViewControlManager->getListOfImageViewControls();
    for (auto* vc : vControls)
    {
        if (vc && vc->isMaximized())
        {
            ret.push_back(vc);
        }
    }

    return ret;
}

std::list<ImageViewControl*> WindowLayoutCoordinator::getListOfViewControls() const
{
    std::list<ImageViewControl*> ret;

    if (!m_dependencies.imageViewControlManager)
    {
        return ret;
    }

    auto vControls = m_dependencies.imageViewControlManager->getListOfImageViewControls();
    for (auto* vc : vControls)
    {
        if (vc)
        {
            ret.push_back(vc);
        }
    }

    return ret;
}

std::list<ImageViewControl*>
WindowLayoutCoordinator::getListOfViewControlsSharingTheSameWindow(ImageViewControl* currentViewControl) const
{
    if (!currentViewControl || !m_dependencies.viewsSharingTheWindowOf)
    {
        return {};
    }
    return m_dependencies.viewsSharingTheWindowOf(*currentViewControl);
}

void WindowLayoutCoordinator::maximizeViewControl(ImageViewControl* ctrl) const
{
    if (!ctrl || !m_dependencies.ui)
    {
        return;
    }

    auto* maximizedControl = getViewControlInAMaximizedContainer(ctrl);
    if (maximizedControl)
    {
        maximizedControl->switchMaximized();
    }

    auto* dock = ctrl->getDockWidget();
    if (!dock)
    {
        return;
    }

    dock->setWidget(new QWidget());
    m_dependencies.ui->centralWidget->insertWidget(2, ctrl);
    m_dependencies.ui->centralWidget->setCurrentIndex(2);

    const auto [iconName, actionText] = maximizeActionPresentation(true);
    m_dependencies.ui->actionMaximize_View->setIcon(m_dependencies.iconProvider ? m_dependencies.iconProvider(iconName)
                                                                                : QIcon());
    m_dependencies.ui->actionMaximize_View->setText(actionText);

    const bool layoutLocked = m_dependencies.layoutIsLocked ? m_dependencies.layoutIsLocked() : false;
    if (!layoutLocked && m_dependencies.lockLayout)
    {
        if (m_dependencies.unlockLayoutWhenRestoring)
        {
            *m_dependencies.unlockLayoutWhenRestoring = true;
        }
        m_dependencies.lockLayout();
    }

    m_dependencies.ui->actionUnlock_Layout->setEnabled(false);
    m_dependencies.ui->actionLock_Layout->setEnabled(false);

    if (m_dependencies.toolbarLockLayout && *m_dependencies.toolbarLockLayout)
    {
        (*m_dependencies.toolbarLockLayout)->setEnabled(false);
    }

    QTimer::singleShot(0, m_dependencies.timerContext, [this]() {
        if (!m_dependencies.ui)
        {
            return;
        }

        m_dependencies.ui->actionGroup_All_Images->setEnabled(false);
        m_dependencies.ui->actionSplit_All_Images->setEnabled(false);
    });

    if (m_dependencies.setActiveImageViewControl)
    {
        m_dependencies.setActiveImageViewControl(ctrl);
    }
}

void WindowLayoutCoordinator::restoreViewControl(ImageViewControl* ctrl) const
{
    if (!ctrl || !m_dependencies.ui)
    {
        return;
    }

    m_dependencies.ui->centralWidget->removeWidget(ctrl);

    auto* dockWidget = ctrl->getDockWidget();
    if (!dockWidget)
    {
        return;
    }

    auto* tmpWidget = dockWidget->widget();

    dockWidget->setWidget(ctrl);
    if (tmpWidget && tmpWidget != ctrl)
    {
        delete tmpWidget;
    }

    ctrl->show();

    m_dependencies.ui->centralWidget->setCurrentIndex(1);

    const auto [iconName, actionText] = maximizeActionPresentation(false);
    m_dependencies.ui->actionMaximize_View->setIcon(m_dependencies.iconProvider ? m_dependencies.iconProvider(iconName)
                                                                                : QIcon());
    m_dependencies.ui->actionMaximize_View->setText(actionText);

    auto maximizedVControls = getListOfMaximizedViewControls();
    if (maximizedVControls.empty())
    {
        if (m_dependencies.unlockLayoutWhenRestoring && *m_dependencies.unlockLayoutWhenRestoring)
        {
            *m_dependencies.unlockLayoutWhenRestoring = false;
            if (m_dependencies.unlockLayout)
            {
                m_dependencies.unlockLayout();
            }
        }

        if (m_dependencies.toolbarLockLayout && *m_dependencies.toolbarLockLayout)
        {
            (*m_dependencies.toolbarLockLayout)->setEnabled(true);
        }

        QTimer::singleShot(0, m_dependencies.timerContext, [this]() {
            if (!m_dependencies.ui)
            {
                return;
            }

            m_dependencies.ui->actionGroup_All_Images->setEnabled(true);
            m_dependencies.ui->actionSplit_All_Images->setEnabled(true);
        });
    }

    if (m_dependencies.setActiveImageViewControl)
    {
        m_dependencies.setActiveImageViewControl(ctrl);
    }
}

} // namespace an::ui
