// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/WindowLayoutCoordinator.h"

#include "app/AfterNightAppUiBuilder.h"

#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/MainDashboard.h"

#include <kddockwidgets/Config.h>
#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>
#include "kddockwidgets/core/DropArea.h"
#include "kddockwidgets/core/FloatingWindow.h"
#include "kddockwidgets/core/MainWindow.h"

#include <QComboBox>
#include <QContextMenuEvent>
#include <QList>
#include <QMenu>
#include <QScrollArea>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include <type_traits>

namespace
{

template <typename ContainerType>
void layout_helper_for_split_operations_impl(ContainerType* container,
                                             QList<KDDockWidgets::QtWidgets::DockWidget*>& dwlist)
{
    auto addDockWidget = [&](KDDockWidgets::QtWidgets::DockWidget* dockWidget, KDDockWidgets::Location location,
                             KDDockWidgets::QtWidgets::DockWidget* relativeTo = nullptr) {
        if constexpr (std::is_same_v<ContainerType, KDDockWidgets::QtWidgets::MainWindow>)
        {
            if (relativeTo)
            {
                container->addDockWidget(dockWidget, location, relativeTo);
            }
            else
            {
                container->addDockWidget(dockWidget, location);
            }
        }
        else
        {
            if (relativeTo)
            {
                container->addDockWidgetToContainingWindow(dockWidget, location, relativeTo);
            }
            else
            {
                container->addDockWidgetToContainingWindow(dockWidget, location);
            }
        }
    };

    auto layoutEqually = [&]() {
        if constexpr (std::is_same_v<ContainerType, KDDockWidgets::QtWidgets::MainWindow>)
        {
            container->layoutEqually();
        }
        else
        {
            auto* fWin = container->dockWidget()->floatingWindow();
            if (fWin)
            {
                fWin->dropArea()->layoutEqually();
            }
            else
            {
                auto* mWin = container->dockWidget()->mainWindow();
                if (mWin)
                {
                    mWin->dropArea()->layoutEqually();
                }
            }
        }
    };

    if (dwlist.size() == 1)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
    }
    else if (dwlist.size() == 2)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
        addDockWidget(dwlist[1], KDDockWidgets::Location_OnRight, dwlist[0]);
    }
    else if (dwlist.size() == 3)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
        addDockWidget(dwlist[1], KDDockWidgets::Location_OnBottom, dwlist[0]);
        addDockWidget(dwlist[2], KDDockWidgets::Location_OnRight);
    }
    else if (dwlist.size() == 4)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
        addDockWidget(dwlist[1], KDDockWidgets::Location_OnRight, dwlist[0]);
        addDockWidget(dwlist[2], KDDockWidgets::Location_OnBottom, dwlist[0]);
        addDockWidget(dwlist[3], KDDockWidgets::Location_OnBottom, dwlist[1]);
    }
    else if (dwlist.size() == 5)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
        addDockWidget(dwlist[1], KDDockWidgets::Location_OnRight, dwlist[0]);
        addDockWidget(dwlist[2], KDDockWidgets::Location_OnBottom);
        addDockWidget(dwlist[3], KDDockWidgets::Location_OnRight, dwlist[2]);
        addDockWidget(dwlist[4], KDDockWidgets::Location_OnRight, dwlist[3]);
    }
    else if (dwlist.size() == 6)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
        addDockWidget(dwlist[1], KDDockWidgets::Location_OnRight, dwlist[0]);
        addDockWidget(dwlist[2], KDDockWidgets::Location_OnRight, dwlist[1]);
        addDockWidget(dwlist[3], KDDockWidgets::Location_OnBottom, dwlist[0]);
        addDockWidget(dwlist[4], KDDockWidgets::Location_OnBottom, dwlist[1]);
        addDockWidget(dwlist[5], KDDockWidgets::Location_OnBottom, dwlist[2]);
    }
    else if (dwlist.size() == 7)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
        addDockWidget(dwlist[1], KDDockWidgets::Location_OnRight, dwlist[0]);
        addDockWidget(dwlist[2], KDDockWidgets::Location_OnRight, dwlist[1]);
        addDockWidget(dwlist[3], KDDockWidgets::Location_OnRight, dwlist[2]);

        addDockWidget(dwlist[4], KDDockWidgets::Location_OnBottom);
        addDockWidget(dwlist[5], KDDockWidgets::Location_OnRight, dwlist[4]);
        addDockWidget(dwlist[6], KDDockWidgets::Location_OnRight, dwlist[5]);
    }
    else if (dwlist.size() == 8)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
        addDockWidget(dwlist[1], KDDockWidgets::Location_OnRight, dwlist[0]);
        addDockWidget(dwlist[2], KDDockWidgets::Location_OnRight, dwlist[1]);
        addDockWidget(dwlist[3], KDDockWidgets::Location_OnRight, dwlist[2]);

        addDockWidget(dwlist[4], KDDockWidgets::Location_OnBottom, dwlist[0]);
        addDockWidget(dwlist[5], KDDockWidgets::Location_OnBottom, dwlist[1]);
        addDockWidget(dwlist[6], KDDockWidgets::Location_OnBottom, dwlist[2]);
        addDockWidget(dwlist[7], KDDockWidgets::Location_OnBottom, dwlist[3]);
    }
    else if (dwlist.size() == 9)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
        addDockWidget(dwlist[1], KDDockWidgets::Location_OnRight, dwlist[0]);
        addDockWidget(dwlist[2], KDDockWidgets::Location_OnRight, dwlist[1]);
        addDockWidget(dwlist[3], KDDockWidgets::Location_OnBottom, dwlist[0]);
        addDockWidget(dwlist[4], KDDockWidgets::Location_OnBottom, dwlist[1]);
        addDockWidget(dwlist[5], KDDockWidgets::Location_OnBottom, dwlist[2]);
        addDockWidget(dwlist[6], KDDockWidgets::Location_OnBottom, dwlist[3]);
        addDockWidget(dwlist[7], KDDockWidgets::Location_OnBottom, dwlist[4]);
        addDockWidget(dwlist[8], KDDockWidgets::Location_OnBottom, dwlist[5]);
    }
    else if (dwlist.size() == 10)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
        addDockWidget(dwlist[1], KDDockWidgets::Location_OnRight, dwlist[0]);
        addDockWidget(dwlist[2], KDDockWidgets::Location_OnRight, dwlist[1]);
        addDockWidget(dwlist[3], KDDockWidgets::Location_OnRight, dwlist[2]);
        addDockWidget(dwlist[4], KDDockWidgets::Location_OnRight, dwlist[3]);

        addDockWidget(dwlist[5], KDDockWidgets::Location_OnBottom, dwlist[0]);
        addDockWidget(dwlist[6], KDDockWidgets::Location_OnBottom, dwlist[1]);
        addDockWidget(dwlist[7], KDDockWidgets::Location_OnBottom, dwlist[2]);
        addDockWidget(dwlist[8], KDDockWidgets::Location_OnBottom, dwlist[3]);
        addDockWidget(dwlist[9], KDDockWidgets::Location_OnBottom, dwlist[4]);
    }
    else if (dwlist.size() == 11)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
        addDockWidget(dwlist[1], KDDockWidgets::Location_OnRight, dwlist[0]);
        addDockWidget(dwlist[2], KDDockWidgets::Location_OnRight, dwlist[1]);
        addDockWidget(dwlist[3], KDDockWidgets::Location_OnRight, dwlist[2]);

        addDockWidget(dwlist[4], KDDockWidgets::Location_OnBottom, dwlist[0]);
        addDockWidget(dwlist[5], KDDockWidgets::Location_OnBottom, dwlist[1]);
        addDockWidget(dwlist[6], KDDockWidgets::Location_OnBottom, dwlist[2]);
        addDockWidget(dwlist[7], KDDockWidgets::Location_OnBottom, dwlist[3]);

        addDockWidget(dwlist[8], KDDockWidgets::Location_OnBottom);
        addDockWidget(dwlist[9], KDDockWidgets::Location_OnRight, dwlist[8]);
        addDockWidget(dwlist[10], KDDockWidgets::Location_OnRight, dwlist[9]);
    }
    else if (dwlist.size() == 12)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
        addDockWidget(dwlist[1], KDDockWidgets::Location_OnRight, dwlist[0]);
        addDockWidget(dwlist[2], KDDockWidgets::Location_OnRight, dwlist[1]);
        addDockWidget(dwlist[3], KDDockWidgets::Location_OnRight, dwlist[2]);

        addDockWidget(dwlist[4], KDDockWidgets::Location_OnBottom, dwlist[0]);
        addDockWidget(dwlist[5], KDDockWidgets::Location_OnBottom, dwlist[1]);
        addDockWidget(dwlist[6], KDDockWidgets::Location_OnBottom, dwlist[2]);
        addDockWidget(dwlist[7], KDDockWidgets::Location_OnBottom, dwlist[3]);

        addDockWidget(dwlist[8], KDDockWidgets::Location_OnBottom, dwlist[4]);
        addDockWidget(dwlist[9], KDDockWidgets::Location_OnBottom, dwlist[5]);
        addDockWidget(dwlist[10], KDDockWidgets::Location_OnBottom, dwlist[6]);
        addDockWidget(dwlist[11], KDDockWidgets::Location_OnBottom, dwlist[7]);
    }
    else if (dwlist.size() == 13)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
        addDockWidget(dwlist[1], KDDockWidgets::Location_OnRight, dwlist[0]);
        addDockWidget(dwlist[2], KDDockWidgets::Location_OnRight, dwlist[1]);
        addDockWidget(dwlist[3], KDDockWidgets::Location_OnRight, dwlist[2]);

        addDockWidget(dwlist[4], KDDockWidgets::Location_OnBottom, dwlist[0]);
        addDockWidget(dwlist[5], KDDockWidgets::Location_OnBottom, dwlist[1]);
        addDockWidget(dwlist[6], KDDockWidgets::Location_OnBottom, dwlist[2]);
        addDockWidget(dwlist[7], KDDockWidgets::Location_OnBottom, dwlist[3]);

        addDockWidget(dwlist[8], KDDockWidgets::Location_OnBottom);
        addDockWidget(dwlist[9], KDDockWidgets::Location_OnRight, dwlist[8]);
        addDockWidget(dwlist[10], KDDockWidgets::Location_OnRight, dwlist[9]);
        addDockWidget(dwlist[11], KDDockWidgets::Location_OnRight, dwlist[10]);
        addDockWidget(dwlist[12], KDDockWidgets::Location_OnRight, dwlist[11]);
    }
    else if (dwlist.size() == 14)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
        addDockWidget(dwlist[1], KDDockWidgets::Location_OnRight, dwlist[0]);
        addDockWidget(dwlist[2], KDDockWidgets::Location_OnRight, dwlist[1]);
        addDockWidget(dwlist[3], KDDockWidgets::Location_OnRight, dwlist[2]);

        addDockWidget(dwlist[4], KDDockWidgets::Location_OnBottom, dwlist[0]);
        addDockWidget(dwlist[5], KDDockWidgets::Location_OnBottom, dwlist[1]);
        addDockWidget(dwlist[6], KDDockWidgets::Location_OnBottom, dwlist[2]);
        addDockWidget(dwlist[7], KDDockWidgets::Location_OnBottom, dwlist[3]);

        addDockWidget(dwlist[8], KDDockWidgets::Location_OnBottom, dwlist[4]);
        addDockWidget(dwlist[9], KDDockWidgets::Location_OnBottom, dwlist[5]);
        addDockWidget(dwlist[10], KDDockWidgets::Location_OnBottom, dwlist[6]);
        addDockWidget(dwlist[11], KDDockWidgets::Location_OnBottom, dwlist[7]);

        addDockWidget(dwlist[12], KDDockWidgets::Location_OnBottom);
        addDockWidget(dwlist[13], KDDockWidgets::Location_OnRight, dwlist[12]);
    }
    else if (dwlist.size() == 15)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
        addDockWidget(dwlist[1], KDDockWidgets::Location_OnRight, dwlist[0]);
        addDockWidget(dwlist[2], KDDockWidgets::Location_OnRight, dwlist[1]);
        addDockWidget(dwlist[3], KDDockWidgets::Location_OnRight, dwlist[2]);

        addDockWidget(dwlist[4], KDDockWidgets::Location_OnBottom, dwlist[0]);
        addDockWidget(dwlist[5], KDDockWidgets::Location_OnBottom, dwlist[1]);
        addDockWidget(dwlist[6], KDDockWidgets::Location_OnBottom, dwlist[2]);
        addDockWidget(dwlist[7], KDDockWidgets::Location_OnBottom, dwlist[3]);

        addDockWidget(dwlist[8], KDDockWidgets::Location_OnBottom, dwlist[4]);
        addDockWidget(dwlist[9], KDDockWidgets::Location_OnBottom, dwlist[5]);
        addDockWidget(dwlist[10], KDDockWidgets::Location_OnBottom, dwlist[6]);
        addDockWidget(dwlist[11], KDDockWidgets::Location_OnBottom, dwlist[7]);

        addDockWidget(dwlist[12], KDDockWidgets::Location_OnBottom);
        addDockWidget(dwlist[13], KDDockWidgets::Location_OnRight, dwlist[12]);
        addDockWidget(dwlist[14], KDDockWidgets::Location_OnRight, dwlist[13]);
    }
    else if (dwlist.size() >= 16)
    {
        addDockWidget(dwlist[0], KDDockWidgets::Location_OnLeft);
        addDockWidget(dwlist[1], KDDockWidgets::Location_OnRight, dwlist[0]);
        addDockWidget(dwlist[2], KDDockWidgets::Location_OnRight, dwlist[1]);
        addDockWidget(dwlist[3], KDDockWidgets::Location_OnRight, dwlist[2]);

        addDockWidget(dwlist[4], KDDockWidgets::Location_OnBottom, dwlist[0]);
        addDockWidget(dwlist[5], KDDockWidgets::Location_OnBottom, dwlist[1]);
        addDockWidget(dwlist[6], KDDockWidgets::Location_OnBottom, dwlist[2]);
        addDockWidget(dwlist[7], KDDockWidgets::Location_OnBottom, dwlist[3]);

        addDockWidget(dwlist[8], KDDockWidgets::Location_OnBottom, dwlist[4]);
        addDockWidget(dwlist[9], KDDockWidgets::Location_OnBottom, dwlist[5]);
        addDockWidget(dwlist[10], KDDockWidgets::Location_OnBottom, dwlist[6]);
        addDockWidget(dwlist[11], KDDockWidgets::Location_OnBottom, dwlist[7]);

        addDockWidget(dwlist[12], KDDockWidgets::Location_OnBottom, dwlist[8]);
        addDockWidget(dwlist[13], KDDockWidgets::Location_OnBottom, dwlist[9]);
        addDockWidget(dwlist[14], KDDockWidgets::Location_OnBottom, dwlist[10]);
        addDockWidget(dwlist[15], KDDockWidgets::Location_OnBottom, dwlist[11]);

        int currentIteration = 0;

        for (int i = 16; i < dwlist.size(); ++i)
        {
            if (currentIteration == 0)
            {
                addDockWidget(dwlist[i], KDDockWidgets::Location_OnRight);
            }
            else
            {
                addDockWidget(dwlist[i], KDDockWidgets::Location_OnBottom, dwlist[i - 1]);
            }

            ++currentIteration;
            if (currentIteration == 4)
            {
                currentIteration = 0;
            }
        }
    }

    layoutEqually();
}

void layout_helper_for_split_operations(KDDockWidgets::QtWidgets::MainWindow* mainWindow,
                                        QList<KDDockWidgets::QtWidgets::DockWidget*>& dwlist)
{
    layout_helper_for_split_operations_impl(mainWindow, dwlist);
}

void layout_helper_for_split_operations(KDDockWidgets::QtWidgets::DockWidget* containingWindow,
                                        QList<KDDockWidgets::QtWidgets::DockWidget*>& dwlist)
{
    layout_helper_for_split_operations_impl(containingWindow, dwlist);
}

} // anonymous namespace

namespace an::ui
{

WindowLayoutCoordinator::WindowLayoutCoordinator(const Dependencies& dependencies) : m_dependencies(dependencies) {}

void WindowLayoutCoordinator::createContextMenuForViewControl(ImageViewControl* view, QContextMenuEvent* event) const
{
    if (!view || !event || !m_dependencies.ui)
    {
        return;
    }

    QMenu menu(view);
    menu.addAction(m_dependencies.ui->menuActions->menuAction());
    menu.addMenu(m_dependencies.ui->menuProcess);
    menu.addAction(m_dependencies.ui->menuConvert_Format->menuAction());
    menu.addAction(m_dependencies.ui->menuExtract_Channel->menuAction());
    menu.addAction(m_dependencies.ui->menuGeometry->menuAction());
    menu.addAction(m_dependencies.ui->menuInformation->menuAction());
    menu.addAction(m_dependencies.ui->menuMask->menuAction());
    menu.addAction(m_dependencies.ui->menuSequence->menuAction());
    menu.addAction(m_dependencies.ui->menuView->menuAction());

    auto* windowMenu = menu.addMenu("Current Window");
    const QIcon groupIcon = m_dependencies.iconProvider ? m_dependencies.iconProvider("tab_group") : QIcon();
    const QIcon splitIcon = m_dependencies.iconProvider ? m_dependencies.iconProvider("grid_3") : QIcon();
    const QIcon fitIcon = m_dependencies.iconProvider ? m_dependencies.iconProvider("zoom_fit") : QIcon();

    auto* groupAction = windowMenu->addAction(groupIcon, "Group All Images");
    auto* splitAction = windowMenu->addAction(splitIcon, "Split All Images");
    auto* fitAction = windowMenu->addAction(fitIcon, "Fit All Images to Window");

    QObject::connect(groupAction, &QAction::triggered, &menu, [this, view]() { groupAllImages(view); });
    QObject::connect(splitAction, &QAction::triggered, &menu, [this, view]() { splitAllImages(view); });
    QObject::connect(fitAction, &QAction::triggered, &menu, [this, view]() { fitAllImagesToWindow(view); });

    if (shouldDisableWindowLayoutActions(view->isMaximized()))
    {
        groupAction->setEnabled(false);
        splitAction->setEnabled(false);
    }

    // The Image menu's own entry, so it is offered - and enabled - as the menu bar offers it.
    menu.addSeparator();
    menu.addAction(m_dependencies.ui->actionShow_In_File_Browser);

    menu.exec(event->globalPos());
}

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

void WindowLayoutCoordinator::splitAllImages(ImageViewControl* currentViewControl) const
{
    if (!currentViewControl)
    {
        return;
    }

    auto* currentDockWidget = currentViewControl->getDockWidget();
    auto vcList = getListOfViewControlsSharingTheSameWindow(currentViewControl);

    QList<KDDockWidgets::QtWidgets::DockWidget*> dwList;
    for (auto* vc : vcList)
    {
        if (vc)
        {
            dwList.push_back(vc->getDockWidget());
        }
    }

    if (dwList.size() > 1)
    {
        layout_helper_for_split_operations(currentDockWidget, dwList);
    }

    if (m_dependencies.setActiveImageViewControl)
    {
        m_dependencies.setActiveImageViewControl(currentViewControl);
    }
}

void WindowLayoutCoordinator::groupAllImages(ImageViewControl* currentViewControl) const
{
    if (!currentViewControl)
    {
        return;
    }

    auto vcList = getListOfViewControlsSharingTheSameWindow(currentViewControl);

    QList<KDDockWidgets::QtWidgets::DockWidget*> dwList;
    for (auto* vc : vcList)
    {
        if (vc)
        {
            dwList.push_back(vc->getDockWidget());
        }
    }

    if (dwList.empty())
    {
        return;
    }

    dwList[0]->addDockWidgetToContainingWindow(dwList[0], KDDockWidgets::Location_OnLeft);

    for (int i = 1; i < dwList.size(); ++i)
    {
        dwList[0]->addDockWidgetAsTab(dwList[i]);
    }

    if (m_dependencies.setActiveImageViewControl)
    {
        m_dependencies.setActiveImageViewControl(currentViewControl);
    }
}

void WindowLayoutCoordinator::fitAllImagesToWindow(ImageViewControl* currentViewControl) const
{
    if (!currentViewControl)
    {
        return;
    }

    auto allControls = getListOfViewControlsSharingTheSameWindow(currentViewControl);
    for (auto* control : allControls)
    {
        if (control)
        {
            control->fitToWindow();
        }
    }
}

void WindowLayoutCoordinator::splitAllImages() const
{
    if (!m_dependencies.imageViewControlManager || !m_dependencies.dockMainWindow || !*m_dependencies.dockMainWindow)
    {
        return;
    }

    auto* currentViewControl = m_dependencies.imageViewControlManager->getActiveImageViewControl();
    auto dwMap = m_dependencies.imageViewControlManager->getListOfImageViewControls();

    QList<KDDockWidgets::QtWidgets::DockWidget*> dwList;
    for (auto* dw : dwMap)
    {
        if (dw)
        {
            dwList.push_back(dw->getDockWidget());
        }
    }

    if (!dwList.empty())
    {
        layout_helper_for_split_operations(*m_dependencies.dockMainWindow, dwList);
    }

    if (currentViewControl && m_dependencies.setActiveImageViewControl)
    {
        m_dependencies.setActiveImageViewControl(currentViewControl);
    }
}

void WindowLayoutCoordinator::groupAllImages() const
{
    if (!m_dependencies.imageViewControlManager || !m_dependencies.dockMainWindow || !*m_dependencies.dockMainWindow)
    {
        return;
    }

    auto* currentViewControl = m_dependencies.imageViewControlManager->getActiveImageViewControl();
    auto dwList = m_dependencies.imageViewControlManager->getListOfImageViewControls();

    if (dwList.empty())
    {
        return;
    }

    auto* dockMainWindow = *m_dependencies.dockMainWindow;
    dockMainWindow->addDockWidget(dwList[0]->getDockWidget(), KDDockWidgets::Location_OnLeft);

    for (size_t i = 1; i < dwList.size(); ++i)
    {
        dwList[0]->getDockWidget()->addDockWidgetAsTab(dwList[i]->getDockWidget());
    }

    if (currentViewControl && m_dependencies.setActiveImageViewControl)
    {
        m_dependencies.setActiveImageViewControl(currentViewControl);
    }
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
    std::list<ImageViewControl*> ret;

    if (!currentViewControl || !m_dependencies.imageViewControlManager)
    {
        return ret;
    }

    auto* currentDockWidget = currentViewControl->getDockWidget();
    if (!currentDockWidget)
    {
        return ret;
    }

    auto allControls = m_dependencies.imageViewControlManager->getListOfImageViewControls();
    KDDockWidgets::Core::FloatingWindow* floatingWindow = currentDockWidget->dockWidget()->floatingWindow();

    for (auto* vc : allControls)
    {
        if (!vc)
        {
            continue;
        }

        auto* dw = vc->getDockWidget();
        if (!dw)
        {
            continue;
        }

        KDDockWidgets::Core::FloatingWindow* currentFloatingWindow = dw->dockWidget()->floatingWindow();
        if (currentFloatingWindow == floatingWindow)
        {
            ret.push_back(vc);
        }
    }

    return ret;
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
