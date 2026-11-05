// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The decisions the application's coordinators take through the Controls and processes libraries, each a function a
/// coordinator calls: a non-interactive operation refused without an image, the menu's connection guard, the image
/// selector kept in step without signals, the mask overlay's guards, the help and extension notices, the dock's
/// required configuration, the maximize action's presentation, and the questions and budgets the application asks for
/// at start and close.

#include "app/AppLifecycleCoordinator.h"
#include "app/ChannelExtractor.h"
#include "app/DialogOrchestrationCoordinator.h"
#include "app/ExtensionManager.h"
#include "app/MaskOverlayCoordinator.h"
#include "app/MenuActionManager.h"
#include "app/UiBootstrapCoordinator.h"
#include "app/ViewControlCoordinator.h"
#include "app/WindowLayoutCoordinator.h"

#include <QComboBox>
#include <QDir>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace
{

/// A view pointer that is never dereferenced: the functions below pass it on.
ImageViewControl* someView()
{
    return reinterpret_cast<ImageViewControl*>(static_cast<std::uintptr_t>(0x1));
}

} // namespace

// A non-interactive operation with no image to work on says so as an error and runs nothing.
TEST(ChannelExtractorComponent, ChannelExtractor_RejectsWhenNoActiveImage)
{
    int errorCount = 0;
    int runs = 0;
    const bool executed = an::ui::executeSingleImageNonInteractiveOperation(
        nullptr, QStringLiteral("Extract channel"),
        QStringLiteral("No selected image or sequence to extract the channel from"),
        [&runs](ImageViewControl*) { ++runs; }, [&errorCount](const QString&, const QString&) { ++errorCount; });
    EXPECT_FALSE(executed);
    EXPECT_EQ(errorCount, 1);
    EXPECT_EQ(runs, 0);
}

// A non-interactive operation with an image runs once on it and says nothing.
TEST(ChannelExtractorComponent, ChannelExtractor_ExecutesOperationWithActiveImage)
{
    int errorCount = 0;
    std::vector<ImageViewControl*> ranOn;
    const bool executed = an::ui::executeSingleImageNonInteractiveOperation(
        someView(), QStringLiteral("Extract channel"),
        QStringLiteral("No selected image or sequence to extract the channel from"),
        [&ranOn](ImageViewControl* view) { ranOn.push_back(view); },
        [&errorCount](const QString&, const QString&) { ++errorCount; });
    EXPECT_TRUE(executed);
    EXPECT_EQ(ranOn, std::vector<ImageViewControl*>{someView()});
    EXPECT_EQ(errorCount, 0);
}

// The menu's actions are connected once: connected already, they are not connected again.
TEST(MenuActionManagerComponent, MenuActionManager_ConnectionGuardIsIdempotent)
{
    EXPECT_TRUE(an::ui::shouldConnectMenuActions(false));
    EXPECT_FALSE(an::ui::shouldConnectMenuActions(true));
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

// A mask overlay is applied only with an active view, and an overlay choice unchecked by a click is checked again,
// since one overlay is always chosen.
TEST(MaskOverlayCoordinatorComponent, MaskOverlayCoordinator_ToggleMaskFlags_UpdatesActiveView)
{
    EXPECT_TRUE(an::ui::canApplyMaskOverlay(true));
    EXPECT_FALSE(an::ui::canApplyMaskOverlay(false));
    EXPECT_TRUE(an::ui::shouldForceSelectedOverlayCheckedState(false));
    EXPECT_FALSE(an::ui::shouldForceSelectedOverlayCheckedState(true));
}

// Extension updates found at start are counted by the entries that have one, announced only after a check that
// succeeded and found some, and named with the versions they move between.
TEST(DialogOrchestrationCoordinatorComponent,
     DialogOrchestrationCoordinator_StartupExtensionUpdateHelpersSummarizeUpdates)
{
    an::ui::extensions::ExtensionRepositoryBrowseEntry updateEntry;
    updateEntry.name = "Sample AI";
    updateEntry.installed_version = "1.0.0";
    updateEntry.latest_version = "1.1.0";
    updateEntry.install_state = an::ui::extensions::ExtensionRepositoryInstallState::UpdateAvailable;

    an::ui::extensions::ExtensionRepositoryBrowseEntry currentEntry;
    currentEntry.name = "Current Tools";
    currentEntry.installed_version = "2.0.0";
    currentEntry.latest_version = "2.0.0";
    currentEntry.install_state = an::ui::extensions::ExtensionRepositoryInstallState::Installed;

    const std::vector<an::ui::extensions::ExtensionRepositoryBrowseEntry> entries = {updateEntry, currentEntry};
    EXPECT_EQ(an::ui::countExtensionStartupUpdates(entries), 1);

    an::ui::ExtensionStartupUpdateCheckResult result;
    result.success = true;
    result.updates.push_back(updateEntry);
    EXPECT_TRUE(an::ui::shouldNotifyExtensionStartupUpdates(result));
    const QString message = an::ui::extensionStartupUpdateNotificationText(result);
    EXPECT_TRUE(message.contains(QStringLiteral("Sample AI")));
    EXPECT_TRUE(message.contains(QStringLiteral("1.0.0")));
    EXPECT_TRUE(message.contains(QStringLiteral("1.1.0")));

    result.success = false;
    EXPECT_FALSE(an::ui::shouldNotifyExtensionStartupUpdates(result));
}

// The bundled user guide is looked for beside the application first, then where an installation puts it, then in the
// source tree's built site; the online guide is the last resort.
TEST(DialogOrchestrationCoordinatorComponent, DialogOrchestrationCoordinator_OpenHelp_ResolvesBundledUserGuide)
{
    const QString appDir = QStringLiteral("C:/Development/afternight/bin/msvc/release");
    const QStringList candidates = an::ui::bundledUserGuideCandidatePaths(appDir);

    ASSERT_GE(candidates.size(), 4);
    EXPECT_EQ(candidates.front(), QDir(appDir).filePath(QStringLiteral("docs/index.html")));
    EXPECT_TRUE(candidates.contains(QDir(appDir).filePath(QStringLiteral("../share/doc/AfterNight/index.html"))));
    EXPECT_TRUE(candidates.contains(QDir(appDir).filePath(QStringLiteral("../../../docs/user-guide/site/index.html"))));
    EXPECT_STREQ(an::ui::kAfterNightUserGuideOnlineUrl, "https://emruiz81.github.io/afternight/");
}

// The online guide is a fallback unless the policy value turns it off: any of the usual spellings of "on" does.
TEST(DialogOrchestrationCoordinatorComponent, DialogOrchestrationCoordinator_OpenHelp_OnlineFallbackIsPolicyControlled)
{
    EXPECT_TRUE(an::ui::onlineUserGuideFallbackAllowed(QString()));
    EXPECT_TRUE(an::ui::onlineUserGuideFallbackAllowed(QStringLiteral("0")));
    EXPECT_TRUE(an::ui::onlineUserGuideFallbackAllowed(QStringLiteral("false")));
    EXPECT_FALSE(an::ui::onlineUserGuideFallbackAllowed(QStringLiteral("1")));
    EXPECT_FALSE(an::ui::onlineUserGuideFallbackAllowed(QStringLiteral("true")));
    EXPECT_FALSE(an::ui::onlineUserGuideFallbackAllowed(QStringLiteral("yes")));
    EXPECT_FALSE(an::ui::onlineUserGuideFallbackAllowed(QStringLiteral("on")));
}

// A message names its own parent when it has one, and the main window when it does not.
TEST(DialogOrchestrationCoordinatorComponent, DialogOrchestrationCoordinator_NotifyMessage_DefaultsParentToMainWindow)
{
    auto* mainWindow = reinterpret_cast<QWidget*>(static_cast<std::uintptr_t>(0x1));
    auto* otherParent = reinterpret_cast<QWidget*>(static_cast<std::uintptr_t>(0x2));
    EXPECT_EQ(an::ui::resolveDialogParent(mainWindow, nullptr), mainWindow);
    EXPECT_EQ(an::ui::resolveDialogParent(mainWindow, otherParent), otherParent);
}

// The dock manager is configured with the eight flags the views need, in the order they are applied.
TEST(UiBootstrapCoordinatorComponent, UiBootstrapCoordinator_AppliesRequiredDockConfigFlags)
{
    using an::ui::RequiredDockConfigFlag;
    EXPECT_EQ(an::ui::kRequiredDockConfigFlags.size(), 8u);
    EXPECT_EQ(an::ui::kRequiredDockConfigFlags[0], RequiredDockConfigFlag::HideTitleBarWhenTabsVisible);
    EXPECT_EQ(an::ui::kRequiredDockConfigFlags[1], RequiredDockConfigFlag::AlwaysShowTabs);
    EXPECT_EQ(an::ui::kRequiredDockConfigFlags[2], RequiredDockConfigFlag::AllowReorderTabs);
    EXPECT_EQ(an::ui::kRequiredDockConfigFlags[3], RequiredDockConfigFlag::TabsHaveCloseButton);
    EXPECT_EQ(an::ui::kRequiredDockConfigFlags[4], RequiredDockConfigFlag::ShowButtonsOnTabBarIfTitleBarHidden);
    EXPECT_EQ(an::ui::kRequiredDockConfigFlags[5], RequiredDockConfigFlag::AllowSwitchingTabsViaMenu);
    EXPECT_EQ(an::ui::kRequiredDockConfigFlags[6], RequiredDockConfigFlag::NativeTitleBar);
    EXPECT_EQ(an::ui::kRequiredDockConfigFlags[7], RequiredDockConfigFlag::TitleBarIsFocusable);
}

// The window layout actions are off while a view is maximized.
TEST(WindowLayoutCoordinatorComponent, WindowLayoutCoordinator_DisablesWindowActionsWhenMaximized)
{
    EXPECT_TRUE(an::ui::shouldDisableWindowLayoutActions(true));
    EXPECT_FALSE(an::ui::shouldDisableWindowLayoutActions(false));
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

// Closing asks about unsaved changes only when there are some and the person asked to be asked.
TEST(AppLifecycleCoordinatorComponent, AppLifecycleCoordinator_PromptsOnPendingChanges)
{
    EXPECT_TRUE(an::ui::shouldPromptOnUnsavedChanges(true, true));
    EXPECT_FALSE(an::ui::shouldPromptOnUnsavedChanges(false, true));
    EXPECT_FALSE(an::ui::shouldPromptOnUnsavedChanges(true, false));
}

// Closing asks about open process windows only when it did not already ask about unsaved changes, there are windows
// open, and the person asked to be asked.
TEST(AppLifecycleCoordinatorComponent, AppLifecycleCoordinator_PromptsOnOpenProcessesOnlyWithoutPendingChanges)
{
    EXPECT_TRUE(an::ui::shouldPromptOnOpenProcesses(true, false, 2));
    EXPECT_FALSE(an::ui::shouldPromptOnOpenProcesses(true, true, 2));
    EXPECT_FALSE(an::ui::shouldPromptOnOpenProcesses(false, false, 2));
    EXPECT_FALSE(an::ui::shouldPromptOnOpenProcesses(true, false, 0));
}

// The batch and preview thread budgets leave the host's threads for the interface.
TEST(AppLifecycleCoordinatorComponent, AppLifecycleCoordinator_ComputesThreadBudgets)
{
    EXPECT_EQ(an::ui::computeBatchThreadBudget(16), 14);
    EXPECT_EQ(an::ui::computeBatchThreadBudget(4), 3);
    EXPECT_EQ(an::ui::computePreviewThreadBudget(24), 20);
    EXPECT_EQ(an::ui::computePreviewThreadBudget(12), 10);
    EXPECT_EQ(an::ui::computePreviewThreadBudget(4), 3);
}

// A restored main window is never smaller than its minimum size.
TEST(AppLifecycleCoordinatorComponent, AppLifecycleCoordinator_ClampsRestoredWindowSize)
{
    const QSize clamped = an::ui::clampRestoredWindowSize(QSize(640, 480));
    EXPECT_EQ(clamped.width(), an::ui::kMainWindowMinimumWidth);
    EXPECT_EQ(clamped.height(), an::ui::kMainWindowMinimumHeight);
}

// A restored main window is placed where it was only if that place is on one of the screens there are now.
TEST(AppLifecycleCoordinatorComponent, AppLifecycleCoordinator_DetectsVisibleGeometryAcrossScreens)
{
    const QList<QRect> screens = {QRect(0, 0, 1920, 1080), QRect(1920, 0, 1920, 1080)};
    EXPECT_TRUE(an::ui::intersectsAnyScreen(QRect(100, 100, 900, 700), screens));
    EXPECT_TRUE(an::ui::intersectsAnyScreen(QRect(2500, 100, 900, 700), screens));
    EXPECT_FALSE(an::ui::intersectsAnyScreen(QRect(-5000, -5000, 200, 200), screens));
}
