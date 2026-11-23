// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The view-creation coordinator's interface over documents: the decisions it states inline about whether a view whose
/// load failed is taken back and what a new view's first history step says, which hold whatever the view is made from.

#include "app/ViewCreationCoordinator.h"

#include <gtest/gtest.h>

namespace
{

// A view made in the standard mode is taken back when its load fails; a preview view is not.
TEST(ViewCreationDecisionTest, OnlyAStandardViewIsTakenBackWhenItsLoadFails)
{
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
