// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The classic shell's own units, apart from its window: the builder of its menus, tool bar, status bar and pages over
/// the catalogue's actions; the image selector the window keeps; and the Maximize View action's look. Each came here
/// from the suite below the shell core it was written in, when the unit left Controls for this library.

#include "AfterNightAppUiBuilder.h"
#include "ClassicWindowLayout.h"
#include "ViewControlCoordinator.h"

#include "Test_ProcessFramework/WindowLayoutChecks.h"
#include "fixtures/RecordedActions.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QComboBox>
#include <QList>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QObject>
#include <QString>
#include <QStringList>

#include <algorithm>

namespace
{

/// The menu titled `title` among the entries of a menu bar or a menu, or null.
QMenu* submenuTitled(const QList<QAction*>& entries, const QString& title)
{
    for (QAction* entry : entries)
    {
        QMenu* menu = entry->menu();
        if (menu != nullptr && menu->title() == title)
        {
            return menu;
        }
    }
    return nullptr;
}

} // namespace

// The main window the builder makes takes every font it sets from the design tokens' faces, in points.
TEST(ControlsWindowLayoutTest, TheMainWindowTakesItsFontsFromTheTokens)
{
    QMainWindow mainWindow;
    const an::tests::fixtures::RecordedActions actions(mainWindow);
    (void)an::ui::app::buildAfterNightAppUi(&mainWindow, actions.lookup());
    const QStringList outside = an::tests::processes::framework::fontsOutsideTheTokens(mainWindow);
    EXPECT_TRUE(outside.isEmpty()) << "main window: " << outside.join(QStringLiteral(", ")).toStdString();
}

// The image selector is changed only when it shows another image than the active one.
TEST(ViewControlCoordinatorComponent, ViewControlCoordinator_DetectsSelectionChange)
{
    EXPECT_TRUE(an::ui::shouldUpdateImageSelectorSelection(QStringLiteral("A"), QStringLiteral("B")));
    EXPECT_FALSE(an::ui::shouldUpdateImageSelectorSelection(QStringLiteral("A"), QStringLiteral("A")));
}

// The dashboard is shown again when the last image is gone, and only if there is a dashboard and no image left.
TEST(ViewControlCoordinatorComponent, ViewControlCoordinator_DetectsDashboardSwitchCondition)
{
    EXPECT_TRUE(an::ui::shouldSwitchToMainDashboardAfterListRefresh(true, false));
    EXPECT_FALSE(an::ui::shouldSwitchToMainDashboardAfterListRefresh(false, false));
    EXPECT_FALSE(an::ui::shouldSwitchToMainDashboardAfterListRefresh(true, true));
}

// An active view's dock is raised only when it has one, docked in the main window or floating.
TEST(ViewControlCoordinatorComponent, ViewControlCoordinator_PromotesDockOnlyWhenAttached)
{
    EXPECT_FALSE(an::ui::shouldPromoteActiveDockWidget(false, false, false));
    EXPECT_FALSE(an::ui::shouldPromoteActiveDockWidget(true, false, false));
    EXPECT_TRUE(an::ui::shouldPromoteActiveDockWidget(true, true, false));
    EXPECT_TRUE(an::ui::shouldPromoteActiveDockWidget(true, false, true));
}

// A view registered is added to the image selector, chosen there and given its tooltip, without the selector telling
// anyone its choice changed.
TEST(ViewControlCoordinatorComponent, ViewControlCoordinator_RegistersImageSelectorEntryWithoutSignals)
{
    QComboBox imageSelector;
    int changedCount = 0;
    QObject::connect(&imageSelector, &QComboBox::currentTextChanged,
                     [&changedCount](const QString&) { ++changedCount; });

    an::ui::syncRegisteredImageSelectorEntry(&imageSelector, QStringLiteral("new_view"), QStringLiteral("tooltip"));

    EXPECT_EQ(imageSelector.count(), 1);
    EXPECT_EQ(imageSelector.currentText(), QStringLiteral("new_view"));
    EXPECT_EQ(imageSelector.itemData(0, Qt::ToolTipRole).toString(), QStringLiteral("tooltip"));
    EXPECT_EQ(changedCount, 0);
}

// The active view is chosen in the image selector without the selector telling anyone its choice changed.
TEST(ViewControlCoordinatorComponent, ViewControlCoordinator_SelectsActiveImageWithoutSignals)
{
    QComboBox imageSelector;
    imageSelector.addItem(QStringLiteral("first"));
    imageSelector.addItem(QStringLiteral("second"));
    int changedCount = 0;
    QObject::connect(&imageSelector, &QComboBox::currentTextChanged,
                     [&changedCount](const QString&) { ++changedCount; });

    an::ui::syncActiveImageSelectorSelection(&imageSelector, QStringLiteral("second"));

    EXPECT_EQ(imageSelector.currentText(), QStringLiteral("second"));
    EXPECT_EQ(changedCount, 0);
}

// A maximized view's action offers to restore it.
TEST(WindowLayoutCoordinatorComponent, WindowLayoutCoordinator_UsesRestorePresentationWhenMaximized)
{
    const auto [iconName, actionText] = an::ui::maximizeActionPresentation(true);
    EXPECT_EQ(iconName, "fullscreen_exit");
    EXPECT_EQ(actionText, QStringLiteral("Restore View"));
}

// A view that is not maximized offers to maximize it.
TEST(WindowLayoutCoordinatorComponent, WindowLayoutCoordinator_UsesMaximizePresentationWhenRestored)
{
    const auto [iconName, actionText] = an::ui::maximizeActionPresentation(false);
    EXPECT_EQ(iconName, "fullscreen");
    EXPECT_EQ(actionText, QStringLiteral("Maximize View"));
}

// The main window is built with its menu bar, tool bar, status bar and central stack in place, the menu bar holding
// the application's menus.
TEST(ControlsWindowLayoutTest, TheMainWindowsMenusAreBuilt)
{
    QMainWindow host;
    const an::tests::fixtures::RecordedActions actions(host);
    const auto ui = an::ui::app::buildAfterNightAppUi(&host, actions.lookup());
    ASSERT_NE(ui.centralWidget, nullptr);
    ASSERT_NE(ui.menuBar, nullptr);
    ASSERT_NE(ui.mainToolBar, nullptr);
    ASSERT_NE(ui.statusBar, nullptr);
    EXPECT_EQ(host.menuBar(), ui.menuBar);
    EXPECT_EQ(host.centralWidget(), ui.centralWidget);
    EXPECT_GT(ui.menuBar->actions().size(), 0);
}

// The application's Process menu holds a Mask Generation menu, and Mask Drawing is one of its entries.
TEST(MaskDrawingRegistrationTest, TheProcessMenusMaskGenerationMenuOffersMaskDrawing)
{
    QMainWindow window;
    const an::tests::fixtures::RecordedActions actions(window);
    ASSERT_TRUE(actions.loaded());
    const an::ui::app::AfterNightAppUiElements ui = an::ui::app::buildAfterNightAppUi(&window, actions.lookup());

    QMenu* const process = submenuTitled(window.menuBar()->actions(), QStringLiteral("Process"));
    ASSERT_NE(process, nullptr);
    QMenu* const maskGeneration = submenuTitled(process->actions(), QStringLiteral("Mask Generation"));
    ASSERT_NE(maskGeneration, nullptr);
    const QList<QAction*> entries = maskGeneration->actions();
    const auto drawing = std::find_if(entries.cbegin(), entries.cend(), [](const QAction* entry) {
        return entry->text() == QStringLiteral("Mask Drawing");
    });
    ASSERT_TRUE(drawing != entries.cend());
    EXPECT_EQ(*drawing, ui.actionMask_Drawing);
}
