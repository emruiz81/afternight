// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightShellClassicExport.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QString>

class ImageInfoStatusBarWidget;
class MainDashboard;
class QComboBox;
class QScrollArea;
class QToolBar;
class QToolButton;
class QWidget;

namespace KDDockWidgets::QtWidgets
{
class MainWindow;
}

namespace an::ui::app
{
class AfterNightAppUiElements;
}

namespace an::ui
{

inline bool isUiBootstrapOrderValid(const std::vector<std::string>& steps)
{
    const std::vector<std::string> expected = {
        "initializeMainDashboard",
        "initializeDockManager",
        "initializeMaximizedView",
    };
    return steps == expected;
}

inline constexpr size_t kMainDashboardDelegateBindingCount = 11;

class AFTERNIGHTSHELLCLASSIC_API ClassicUiBootstrap
{
public:
    struct Dependencies
    {
        an::ui::app::AfterNightAppUiElements* ui = nullptr;
        QWidget* mainWindow = nullptr;
        QComboBox* imageViewControlListComboBox = nullptr;
        std::unique_ptr<ImageInfoStatusBarWidget>* imageInfoStatusBarWidget = nullptr;
        MainDashboard** mainDashboard = nullptr;
        QScrollArea** mainDashboardScrollArea = nullptr;
        KDDockWidgets::QtWidgets::MainWindow** dockMainWindow = nullptr;
        QToolBar** dockManagerToolBar = nullptr;
        QToolButton** toolbarLockLayout = nullptr;

        std::function<void()> connectActions;
        std::function<void()> refreshTheme;
        std::function<void()> onOpenFileRequested;
        std::function<void()> onNewSequenceRequested;
        std::function<void()> onImageIntegrationRequested;
        std::function<void()> onOpenExtensionManagerRequested;
        std::function<void()> onClearRecentFilesRequested;
        std::function<void()> onOpenSettingsRequested;
        std::function<void()> onSystemInformationRequested;
        std::function<void()> onAboutRequested;
        std::function<void()> onOpenHelpRequested;
        std::function<void(const QString&)> onOpenRecentFileRequested;
        std::function<void(const QString&)> onRemoveRecentFileRequested;
    };

    explicit ClassicUiBootstrap(const Dependencies& dependencies);

    void finalizeConstructorUiSetup() const;
    void initializeMainDashboard() const;
    void initializeDockManager() const;
    void initializeMaximizedView() const;

private:
    Dependencies m_dependencies;
};

} // namespace an::ui
