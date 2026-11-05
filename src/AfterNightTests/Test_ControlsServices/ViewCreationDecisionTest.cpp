// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The view-creation coordinator's interface over documents: the decisions it states inline
/// about where a new view goes and what its first history step says, which hold whatever the view is made from.

#include "app/ViewCreationCoordinator.h"

#include <gtest/gtest.h>

namespace
{

TEST(ViewCreationDecisionTest, AStandardViewLeavesTheDashboardForTheDockManager)
{
    const an::ui::NewViewLayoutTransitionDecision fromDashboard =
        an::ui::decideNewViewLayoutTransition(true, true, false, false, false, false);
    EXPECT_TRUE(fromDashboard.shouldSwitchToDockManager);
    EXPECT_FALSE(fromDashboard.shouldRestoreMaximizedActiveView);

    const an::ui::NewViewLayoutTransitionDecision overMaximized =
        an::ui::decideNewViewLayoutTransition(true, false, false, true, false, true);
    EXPECT_TRUE(overMaximized.shouldSwitchToDockManager);
    EXPECT_TRUE(overMaximized.shouldRestoreMaximizedActiveView);

    const an::ui::NewViewLayoutTransitionDecision floating =
        an::ui::decideNewViewLayoutTransition(true, false, false, true, true, true);
    EXPECT_FALSE(floating.shouldSwitchToDockManager);
    EXPECT_FALSE(floating.shouldRestoreMaximizedActiveView);
}

TEST(ViewCreationDecisionTest, APreviewViewChangesNoLayout)
{
    const an::ui::NewViewLayoutTransitionDecision preview =
        an::ui::decideNewViewLayoutTransition(false, true, false, true, false, true);
    EXPECT_FALSE(preview.shouldSwitchToDockManager);
    EXPECT_FALSE(preview.shouldRestoreMaximizedActiveView);
    EXPECT_FALSE(an::ui::shouldRollbackCreatedControlAfterLoadFailure(false));
    EXPECT_TRUE(an::ui::shouldRollbackCreatedControlAfterLoadFailure(true));
}

TEST(ViewCreationDecisionTest, AViewsFirstHistoryStepAndIconSayWhatItWasMadeFrom)
{
    EXPECT_EQ(an::ui::resolveCreatedViewHistoryDescription("", false), "Image created internally");
    EXPECT_EQ(an::ui::resolveCreatedViewHistoryDescription("", true), "Sequence created internally");
    EXPECT_EQ(an::ui::resolveCreatedViewHistoryDescription("Stacked", true), "Stacked");
    EXPECT_STREQ(an::ui::iconNameForCreatedView(false), "picture");
    EXPECT_STREQ(an::ui::iconNameForCreatedView(true), "film");
}

} // namespace
