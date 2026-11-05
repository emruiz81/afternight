// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QMainWindow>
#include <QMessageBox>
#include <QSize>

#include "app/AppLifecycleCoordinator.h"
#include "controls/ImageViewControlManager.h"

TEST(AfterNightAppWindowGeometrySuite, MainWindowMinimumSizeTargets720pLayouts)
{
    EXPECT_EQ(an::ui::kMainWindowMinimumWidth, 1200);
    EXPECT_EQ(an::ui::kMainWindowMinimumHeight, 720);
}

TEST(AfterNightAppWindowGeometrySuite, ClampRestoredWindowSizeHonorsMainWindowMinimums)
{
    const QSize clamped = an::ui::clampRestoredWindowSize(QSize(640, 480));

    EXPECT_EQ(clamped.width(), an::ui::kMainWindowMinimumWidth);
    EXPECT_EQ(clamped.height(), an::ui::kMainWindowMinimumHeight);
}

TEST(AfterNightAppWindowGeometrySuite, ClampRestoredWindowSizePreservesLargerSizes)
{
    const QSize original(1536, 864);

    EXPECT_EQ(an::ui::clampRestoredWindowSize(original), original);
}

TEST(AfterNightAppLifecycleCloseSuite, PrepareForCloseStopsOpenProcessesAfterConfirmation)
{
    ImageViewControlManager image_view_control_manager;
    QMainWindow main_window;
    bool close_open_processes_called = false;
    int prompt_count = 0;

    an::ui::AppLifecycleCoordinator::Dependencies dependencies;
    dependencies.mainWindow = &main_window;
    dependencies.imageViewControlManager = &image_view_control_manager;
    dependencies.getNumOfOpenProcesses = []() { return static_cast<size_t>(2); };
    dependencies.closeOpenProcesses = [&close_open_processes_called]() { close_open_processes_called = true; };
    dependencies.messageBoxYesNo = [&prompt_count](QWidget*, const QString&, const QString&, bool, int* ret) {
        ++prompt_count;
        if (ret)
        {
            *ret = QMessageBox::Yes;
        }
    };

    an::ui::AppLifecycleCoordinator coordinator(dependencies);

    EXPECT_TRUE(coordinator.prepareForClose());
    EXPECT_EQ(prompt_count, 1);
    EXPECT_TRUE(close_open_processes_called);
}

TEST(AfterNightAppLifecycleCloseSuite, PrepareForCloseCancelsBackgroundWorkAfterCloseIsAccepted)
{
    ImageViewControlManager image_view_control_manager;
    QMainWindow main_window;
    bool cancel_background_work_called = false;

    an::ui::AppLifecycleCoordinator::Dependencies dependencies;
    dependencies.mainWindow = &main_window;
    dependencies.imageViewControlManager = &image_view_control_manager;
    dependencies.cancelBackgroundWork = [&cancel_background_work_called]() { cancel_background_work_called = true; };

    an::ui::AppLifecycleCoordinator coordinator(dependencies);

    EXPECT_TRUE(coordinator.prepareForClose());
    EXPECT_TRUE(cancel_background_work_called);
}

TEST(AfterNightAppLifecycleCloseSuite, PrepareForCloseLeavesOpenProcessesRunningWhenUserCancels)
{
    ImageViewControlManager image_view_control_manager;
    QMainWindow main_window;
    bool cancel_background_work_called = false;
    bool close_open_processes_called = false;
    int prompt_count = 0;

    an::ui::AppLifecycleCoordinator::Dependencies dependencies;
    dependencies.mainWindow = &main_window;
    dependencies.imageViewControlManager = &image_view_control_manager;
    dependencies.cancelBackgroundWork = [&cancel_background_work_called]() { cancel_background_work_called = true; };
    dependencies.getNumOfOpenProcesses = []() { return static_cast<size_t>(1); };
    dependencies.closeOpenProcesses = [&close_open_processes_called]() { close_open_processes_called = true; };
    dependencies.messageBoxYesNo = [&prompt_count](QWidget*, const QString&, const QString&, bool, int* ret) {
        ++prompt_count;
        if (ret)
        {
            *ret = QMessageBox::No;
        }
    };

    an::ui::AppLifecycleCoordinator coordinator(dependencies);

    EXPECT_FALSE(coordinator.prepareForClose());
    EXPECT_EQ(prompt_count, 1);
    EXPECT_FALSE(cancel_background_work_called);
    EXPECT_FALSE(close_open_processes_called);
}
