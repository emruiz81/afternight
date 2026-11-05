// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "widgets/ImageInfoStatusBarWidget.h"

TEST(ImageViewControlStatusBarCoordinates, FormatSexagesimalRa)
{
    EXPECT_EQ(an::ui::formatRightAscensionSexagesimal(187.5), QStringLiteral("12h 30m 00.00s"));
}

TEST(ImageViewControlStatusBarCoordinates, FormatSexagesimalDec)
{
    EXPECT_EQ(an::ui::formatDeclinationSexagesimal(-45.5), QStringLiteral("-45° 30' 00.0\""));
}

TEST(ImageViewControlStatusBarCoordinates, FormatSexagesimalRaWrapsNegativeDegrees)
{
    EXPECT_EQ(an::ui::formatRightAscensionSexagesimal(-15.0), QStringLiteral("23h 00m 00.00s"));
}
