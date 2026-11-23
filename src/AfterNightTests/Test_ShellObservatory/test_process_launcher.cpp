// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "app/AppLifecycleCoordinator.h"

TEST(AfterNightAppProcessSuite, LifecycleHelpersPromptOnlyWhenRequired)
{
    EXPECT_TRUE(an::ui::shouldPromptOnUnsavedChanges(true, true));
    EXPECT_FALSE(an::ui::shouldPromptOnUnsavedChanges(false, true));

    EXPECT_TRUE(an::ui::shouldPromptOnOpenProcesses(true, false, 1));
    EXPECT_FALSE(an::ui::shouldPromptOnOpenProcesses(true, true, 1));
    EXPECT_FALSE(an::ui::shouldPromptOnOpenProcesses(true, false, 0));
}

TEST(AfterNightAppProcessSuite, LifecycleHelpersComputeThreadBudgets)
{
    EXPECT_EQ(an::ui::computeBatchThreadBudget(16), 14);
    EXPECT_EQ(an::ui::computeBatchThreadBudget(4), 3);

    EXPECT_EQ(an::ui::computePreviewThreadBudget(24), 20);
    EXPECT_EQ(an::ui::computePreviewThreadBudget(12), 10);
    EXPECT_EQ(an::ui::computePreviewThreadBudget(4), 3);
}
