// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/UiBootstrapCoordinator.h"

#include "app/AfterNightAppUiBuilder.h"

#include "widgets/ImageInfoStatusBarWidget.h"
#include "controls/KDDockWidgetsHelper.h"
#include "controls/MainDashboard.h"
#include "theme/KddwProxyStyle.h"

#include <QComboBox>
#include <QObject>
#include <QScrollArea>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/MainWindow.h>

namespace an::ui
{

namespace
{
KDDockWidgets::Config::Flags applyRequiredDockConfigFlags(KDDockWidgets::Config::Flags flags)
{
    for (const auto requiredFlag : kRequiredDockConfigFlags)
    {
        switch (requiredFlag)
        {
        case RequiredDockConfigFlag::HideTitleBarWhenTabsVisible:
            flags |= KDDockWidgets::Config::Flag_HideTitleBarWhenTabsVisible;
            break;
        case RequiredDockConfigFlag::AlwaysShowTabs:
            flags |= KDDockWidgets::Config::Flag_AlwaysShowTabs;
            break;
        case RequiredDockConfigFlag::AllowReorderTabs:
            flags |= KDDockWidgets::Config::Flag_AllowReorderTabs;
            break;
        case RequiredDockConfigFlag::TabsHaveCloseButton:
            flags |= KDDockWidgets::Config::Flag_TabsHaveCloseButton;
            break;
        case RequiredDockConfigFlag::ShowButtonsOnTabBarIfTitleBarHidden:
            flags |= KDDockWidgets::Config::Flag_ShowButtonsOnTabBarIfTitleBarHidden;
            break;
        case RequiredDockConfigFlag::AllowSwitchingTabsViaMenu:
            flags |= KDDockWidgets::Config::Flag_AllowSwitchingTabsViaMenu;
            break;
        case RequiredDockConfigFlag::NativeTitleBar:
            flags |= KDDockWidgets::Config::Flag_NativeTitleBar;
            break;
        case RequiredDockConfigFlag::TitleBarIsFocusable:
            flags |= KDDockWidgets::Config::Flag_TitleBarIsFocusable;
            break;
        }
    }
    return flags;
}
} // namespace

UiBootstrapCoordinator::UiBootstrapCoordinator(const Dependencies& dependencies) : m_dependencies(dependencies) {}

void UiBootstrapCoordinator::finalizeConstructorUiSetup() const
{
    if (!m_dependencies.ui || !m_dependencies.mainWindow || !m_dependencies.imageViewControlListComboBox)
    {
        return;
    }

    if (m_dependencies.refreshTheme)
    {
        m_dependencies.refreshTheme();
    }

    m_dependencies.ui->mainToolBar->setAutoFillBackground(true);
    m_dependencies.ui->mainToolBar->setContentsMargins(0, 0, 0, 0);
    // Remove the gap Qt's QMainWindowLayout inserts between the toolbar dock area
    // and the central widget (spacing + outer margins of the internal layout).
    if (auto* mainLayout = m_dependencies.mainWindow->layout())
    {
        mainLayout->setSpacing(0);
        mainLayout->setContentsMargins(0, 0, 0, 0);
    }
    m_dependencies.imageViewControlListComboBox->setFixedSize(250, 22);
    m_dependencies.imageViewControlListComboBox->setEditable(false);
    m_dependencies.imageViewControlListComboBox->setEnabled(false);
    m_dependencies.imageViewControlListComboBox->setObjectName("mainImageViewSelectorCombo");
    m_dependencies.ui->statusBar->addWidget(m_dependencies.imageViewControlListComboBox);

    if (m_dependencies.imageInfoStatusBarWidget)
    {
        *m_dependencies.imageInfoStatusBarWidget = std::make_unique<ImageInfoStatusBarWidget>();
        if (*m_dependencies.imageInfoStatusBarWidget)
        {
            (*m_dependencies.imageInfoStatusBarWidget)->initialize(m_dependencies.ui->statusBar);
        }
    }

    m_dependencies.ui->actionLog_Viewer->setEnabled(true);
    m_dependencies.ui->actionUnlock_Layout->setEnabled(false);
    m_dependencies.ui->statusBar->setVisible(false);
    m_dependencies.mainWindow->setAcceptDrops(true);

    if (m_dependencies.connectActions)
    {
        m_dependencies.connectActions();
    }
}

void UiBootstrapCoordinator::initializeMainDashboard() const
{
    if (!m_dependencies.ui || !m_dependencies.mainWindow || !m_dependencies.mainDashboard ||
        !m_dependencies.mainDashboardScrollArea)
    {
        return;
    }

    m_dependencies.ui->statusBar->setVisible(false);
    if (m_dependencies.dockManagerToolBar && *m_dependencies.dockManagerToolBar)
    {
        (*m_dependencies.dockManagerToolBar)->setEnabled(false);
    }
    m_dependencies.ui->menuWindow->setEnabled(false);

    m_dependencies.ui->actionLog_Viewer->setChecked(false);
    m_dependencies.ui->actionLog_Viewer->setEnabled(true);

    *m_dependencies.mainDashboardScrollArea = new QScrollArea(m_dependencies.mainWindow);
    *m_dependencies.mainDashboard = new MainDashboard(*m_dependencies.mainDashboardScrollArea);

    (*m_dependencies.mainDashboardScrollArea)->setWidget(*m_dependencies.mainDashboard);
    (*m_dependencies.mainDashboardScrollArea)->setWidgetResizable(true);
    m_dependencies.ui->centralWidget->addWidget(*m_dependencies.mainDashboardScrollArea);
    (*m_dependencies.mainDashboard)->initialize();

    QObject::connect(*m_dependencies.mainDashboard, &MainDashboard::openFileRequested, m_dependencies.mainWindow,
                     [this]() {
                         if (m_dependencies.onOpenFileRequested)
                         {
                             m_dependencies.onOpenFileRequested();
                         }
                     });
    QObject::connect(*m_dependencies.mainDashboard, &MainDashboard::newSequenceRequested, m_dependencies.mainWindow,
                     [this]() {
                         if (m_dependencies.onNewSequenceRequested)
                         {
                             m_dependencies.onNewSequenceRequested();
                         }
                     });
    QObject::connect(*m_dependencies.mainDashboard, &MainDashboard::imageIntegrationRequested,
                     m_dependencies.mainWindow, [this]() {
                         if (m_dependencies.onImageIntegrationRequested)
                         {
                             m_dependencies.onImageIntegrationRequested();
                         }
                     });
    QObject::connect(*m_dependencies.mainDashboard, &MainDashboard::openExtensionManagerRequested,
                     m_dependencies.mainWindow, [this]() {
                         if (m_dependencies.onOpenExtensionManagerRequested)
                         {
                             m_dependencies.onOpenExtensionManagerRequested();
                         }
                     });
    QObject::connect(*m_dependencies.mainDashboard, &MainDashboard::clearRecentFilesRequested,
                     m_dependencies.mainWindow, [this]() {
                         if (m_dependencies.onClearRecentFilesRequested)
                         {
                             m_dependencies.onClearRecentFilesRequested();
                         }
                     });
    QObject::connect(*m_dependencies.mainDashboard, &MainDashboard::openSettingsRequested, m_dependencies.mainWindow,
                     [this]() {
                         if (m_dependencies.onOpenSettingsRequested)
                         {
                             m_dependencies.onOpenSettingsRequested();
                         }
                     });
    QObject::connect(*m_dependencies.mainDashboard, &MainDashboard::systemInformationRequested,
                     m_dependencies.mainWindow, [this]() {
                         if (m_dependencies.onSystemInformationRequested)
                         {
                             m_dependencies.onSystemInformationRequested();
                         }
                     });
    QObject::connect(*m_dependencies.mainDashboard, &MainDashboard::aboutRequested, m_dependencies.mainWindow,
                     [this]() {
                         if (m_dependencies.onAboutRequested)
                         {
                             m_dependencies.onAboutRequested();
                         }
                     });
    QObject::connect(*m_dependencies.mainDashboard, &MainDashboard::openHelpRequested, m_dependencies.mainWindow,
                     [this]() {
                         if (m_dependencies.onOpenHelpRequested)
                         {
                             m_dependencies.onOpenHelpRequested();
                         }
                     });
    QObject::connect(*m_dependencies.mainDashboard, &MainDashboard::openRecentFileRequested, m_dependencies.mainWindow,
                     [this](const QString& filePath) {
                         if (m_dependencies.onOpenRecentFileRequested)
                         {
                             m_dependencies.onOpenRecentFileRequested(filePath);
                         }
                     });
    QObject::connect(*m_dependencies.mainDashboard, &MainDashboard::removeRecentFileRequested,
                     m_dependencies.mainWindow, [this](const QString& filePath) {
                         if (m_dependencies.onRemoveRecentFileRequested)
                         {
                             m_dependencies.onRemoveRecentFileRequested(filePath);
                         }
                     });
}

void UiBootstrapCoordinator::initializeDockManager() const
{
    if (!m_dependencies.ui || !m_dependencies.mainWindow || !m_dependencies.dockMainWindow)
    {
        return;
    }

    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);
    KDDockWidgets::MainWindowOptions options = {};
    const Qt::WindowFlags winflags = Qt::WindowFlags();

    auto flags = KDDockWidgets::Config::self().flags();
    flags = applyRequiredDockConfigFlags(flags);
    KDDockWidgets::Config::self().setFlags(flags);

    KDDockWidgets::Config::self().setDraggedWindowOpacity(0.7);
    KDDockWidgets::Config::self().setAbsoluteWidgetMinSize(QSize(100, 100));
    KDDockWidgets::Config::self().setViewFactory(new AN_ViewFactory);

    *m_dependencies.dockMainWindow =
        new KDDockWidgets::QtWidgets::MainWindow("AfterNightApp", options, m_dependencies.mainWindow, winflags);

    // Remove default KDDW center-widget margins so there is no gap between
    // the toolbar area and the dock area.
    (*m_dependencies.dockMainWindow)->setCenterWidgetMargins(QMargins(0, 0, 0, 0));

    // Install the KDDW-specific proxy style on the dock main window so that
    // all dock children (title bars, tabs, separators, groups, floating windows)
    // render using the design token system.
    installKddwProxyStyle(*m_dependencies.dockMainWindow);

    m_dependencies.ui->centralWidget->addWidget(*m_dependencies.dockMainWindow);
}

void UiBootstrapCoordinator::initializeMaximizedView() const
{
    if (!m_dependencies.ui || !m_dependencies.mainWindow)
    {
        return;
    }

    m_dependencies.ui->centralWidget->addWidget(new QWidget());
}

} // namespace an::ui
