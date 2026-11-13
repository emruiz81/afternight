// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The sexagesimal labels of right ascension and declination: hours and minutes, or degrees and arcminutes, as a
/// coarse grid labels its lines, and seconds - whole, as a fine grid labels them, or to a stated number of decimals -
/// each field rounded once, a declination held to the poles, and a coordinate that is no number labelled as zero.

#include <gtest/gtest.h>

#include "SexagesimalFormatter.h"

#include <limits>

TEST(SexagesimalFormatter, ARightAscensionInHoursAndMinutes)
{
    EXPECT_EQ(an::ui::rightAscensionMinutesLabel(0.0), QStringLiteral("00h 00m"));
    EXPECT_EQ(an::ui::rightAscensionMinutesLabel(180.0), QStringLiteral("12h 00m"));
}

TEST(SexagesimalFormatter, ANegativeRightAscensionWrapsIntoTheDay)
{
    EXPECT_EQ(an::ui::rightAscensionMinutesLabel(-15.0), QStringLiteral("23h 00m"));
}

TEST(SexagesimalFormatter, ARightAscensionToATenthOfASecond)
{
    EXPECT_EQ(an::ui::rightAscensionLabel(187.5, 1u), QStringLiteral("12h 30m 00.0s"));
}

TEST(SexagesimalFormatter, ARightAscensionRoundsOnceIntoEveryField)
{
    // 12h 29m 59.96s at a tenth of a second is 12h 30m 00.0s, never 29m 60.0s.
    EXPECT_EQ(an::ui::rightAscensionLabel((12.0 + 29.0 / 60.0 + 59.96 / 3600.0) * 15.0, 1u),
              QStringLiteral("12h 30m 00.0s"));
}

TEST(SexagesimalFormatter, ARightAscensionInWholeSecondsRoundsOnceIntoEveryField)
{
    // A fine grid's label: 10h 29m 59.7s in whole seconds is 10h 30m 00s, never 29m 60s.
    EXPECT_EQ(an::ui::rightAscensionLabel((10.0 + 29.0 / 60.0 + 59.7 / 3600.0) * 15.0, 0u),
              QStringLiteral("10h 30m 00s"));
}

TEST(SexagesimalFormatter, ADeclinationInWholeArcsecondsRoundsOnceIntoEveryField)
{
    // +10° 59' 59.6" in whole arcseconds is +11° 00' 00", never 59' 60".
    EXPECT_EQ(an::ui::declinationLabel(10.0 + 59.0 / 60.0 + 59.6 / 3600.0, 0u), QStringLiteral("+11° 00' 00\""));
}

TEST(SexagesimalFormatter, ADeclinationInDegreesAndArcminutes)
{
    EXPECT_EQ(an::ui::declinationMinutesLabel(45.5), QStringLiteral("+45° 30'"));
    EXPECT_EQ(an::ui::declinationMinutesLabel(-45.5), QStringLiteral("-45° 30'"));
    EXPECT_EQ(an::ui::declinationMinutesLabel(0.0), QStringLiteral("+00° 00'"));
}

TEST(SexagesimalFormatter, ADeclinationToATenthOfAnArcsecond)
{
    EXPECT_EQ(an::ui::declinationLabel(45.0 + 30.0 / 60.0 + 15.0 / 3600.0, 1u), QStringLiteral("+45° 30' 15.0\""));
}

TEST(SexagesimalFormatter, ADeclinationIsHeldToThePoles)
{
    EXPECT_EQ(an::ui::declinationMinutesLabel(95.0), QStringLiteral("+90° 00'"));
    EXPECT_EQ(an::ui::declinationMinutesLabel(-95.0), QStringLiteral("-90° 00'"));
}

TEST(SexagesimalFormatter, ADeclinationThatIsNoNumberIsLabelledAsZero)
{
    EXPECT_EQ(an::ui::declinationMinutesLabel(std::numeric_limits<double>::quiet_NaN()), QStringLiteral("+00° 00'"));
}
