// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include "controls/StaticPreviewCacheLimits.h"
#include "processes/ImageIntegration/ImageIntegrationConstants.h"

TEST(Constants, AllConstantsDefinedInCentralHeader)
{
    EXPECT_EQ(ImageIntegration::Config::kAutosaveIntervalMs, 5000);
    EXPECT_EQ(ImageIntegration::Config::kDebounceIntervalMs, 200);
    EXPECT_EQ(ImageIntegration::Config::kContextPanelWidthPx, 320);
}

TEST(Constants, StaticPreviewCacheLimitsLiveWithTheViewer)
{
    EXPECT_EQ(an::ui::kMaxPreviewCacheEntries, 32u);
    EXPECT_EQ(an::ui::kMaxPreviewMasterCacheEntries, 8u);
}
