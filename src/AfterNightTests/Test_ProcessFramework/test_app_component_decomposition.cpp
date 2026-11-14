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
#include "app/ViewControlCoordinator.h"
#include "app/WindowLayoutCoordinator.h"

#include <QComboBox>
#include <QDir>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
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
    EXPECT_STREQ(an::ui::kAfterNightUserGuideOnlineUrl, "https://docs.afternightastro.org/");
}

// A process's page is looked for where the guide's home page is, in the same order, under the path the process states.
TEST(DialogOrchestrationCoordinatorComponent, DialogOrchestrationCoordinator_OpenHelp_ResolvesAPageOfTheBundledGuide)
{
    const QString appDir = QStringLiteral("C:/Development/afternight/bin/msvc/release");
    const QString page = QStringLiteral("processes/stretching/histogram-transformation.html");
    const QStringList home = an::ui::bundledUserGuideCandidatePaths(appDir);
    const QStringList candidates = an::ui::bundledUserGuideCandidatePaths(appDir, page);

    ASSERT_EQ(candidates.size(), home.size());
    EXPECT_EQ(candidates.front(), QDir(appDir).filePath(QStringLiteral("docs/") + page));
    for (qsizetype index = 0; index < candidates.size(); ++index)
    {
        ASSERT_TRUE(home[index].endsWith(QStringLiteral("index.html")));
        EXPECT_EQ(candidates[index], home[index].chopped(10) + page);
    }
    EXPECT_EQ(an::ui::bundledUserGuideCandidatePaths(appDir, QString()), home);
}

// A path that is no page inside the guide - one that climbs out of it, an absolute one, an address, a folder - is not
// followed: the home page opens instead, from the bundled guide and from the online one alike.
TEST(DialogOrchestrationCoordinatorComponent, DialogOrchestrationCoordinator_OpenHelp_FollowsOnlyAPageInsideTheGuide)
{
    const QString home = QStringLiteral("index.html");
    EXPECT_EQ(an::ui::userGuidePage(QStringLiteral("interface/masks.html")), QStringLiteral("interface/masks.html"));
    EXPECT_EQ(an::ui::userGuidePage(QString()), home);
    for (const char* refused : {"../secrets.html", "processes/../../x.html", "/etc/passwd.html", "C:/x.html",
                                "processes\\x.html", "https://example.org/x.html", "processes/stretching/",
                                "processes//x.html", "./x.html", "page.exe", "x.html?y", "x.html#y"})
    {
        EXPECT_EQ(an::ui::userGuidePage(QString::fromLatin1(refused)), home) << refused;
    }

    const QString appDir = QStringLiteral("C:/Development/afternight/bin/msvc/release");
    EXPECT_EQ(an::ui::bundledUserGuideCandidatePaths(appDir, QStringLiteral("../../x.html")),
              an::ui::bundledUserGuideCandidatePaths(appDir));
}

// The online guide has a process's page under the path the bundled guide has it, and the guide's own address is its
// home page. A policy value that turns the online guide off leaves no address to open, whatever the page.
TEST(DialogOrchestrationCoordinatorComponent, DialogOrchestrationCoordinator_OpenHelp_JoinsThePageToTheOnlineGuide)
{
    const QString page = QStringLiteral("processes/stretching/histogram-transformation.html");
    const std::optional<QUrl> online = an::ui::onlineUserGuideUrl(page, QString());
    ASSERT_TRUE(online.has_value());
    EXPECT_EQ(online->toString(),
              QStringLiteral("https://docs.afternightastro.org/processes/stretching/histogram-transformation.html"));

    const std::optional<QUrl> home = an::ui::onlineUserGuideUrl(QString(), QStringLiteral("0"));
    ASSERT_TRUE(home.has_value());
    EXPECT_EQ(home->toString(), QString::fromLatin1(an::ui::kAfterNightUserGuideOnlineUrl));
    const std::optional<QUrl> outside = an::ui::onlineUserGuideUrl(QStringLiteral("../x.html"), QString());
    ASSERT_TRUE(outside.has_value());
    EXPECT_EQ(outside->toString(), QString::fromLatin1(an::ui::kAfterNightUserGuideOnlineUrl));

    for (const char* off : {"1", "true", "yes", "on", " ON "})
    {
        EXPECT_FALSE(an::ui::onlineUserGuideUrl(page, QString::fromLatin1(off)).has_value()) << off;
        EXPECT_FALSE(an::ui::onlineUserGuideUrl(QString(), QString::fromLatin1(off)).has_value()) << off;
    }
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

