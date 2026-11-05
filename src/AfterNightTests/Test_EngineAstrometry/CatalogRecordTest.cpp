// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The catalog data model: angles, separations and the canonical spelling of a designation.

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

namespace
{

TEST(CatalogRecordTest, RightAscensionFoldsIntoOneTurn)
{
    EXPECT_DOUBLE_EQ(an::normalizeRightAscensionDegrees(0.0), 0.0);
    EXPECT_DOUBLE_EQ(an::normalizeRightAscensionDegrees(359.75), 359.75);
    EXPECT_DOUBLE_EQ(an::normalizeRightAscensionDegrees(360.0), 0.0);
    EXPECT_DOUBLE_EQ(an::normalizeRightAscensionDegrees(370.5), 10.5);
    EXPECT_DOUBLE_EQ(an::normalizeRightAscensionDegrees(-0.25), 359.75);
    EXPECT_DOUBLE_EQ(an::normalizeRightAscensionDegrees(-721.0), 359.0);
    EXPECT_TRUE(std::isnan(an::normalizeRightAscensionDegrees(std::numeric_limits<double>::quiet_NaN())));
}

TEST(CatalogRecordTest, DirectionValidityIsRangeAndFiniteness)
{
    EXPECT_TRUE(an::isSkyCoordinate({.rightAscensionDegrees = 0.0, .declinationDegrees = -90.0}));
    EXPECT_TRUE(an::isSkyCoordinate({.rightAscensionDegrees = 359.999, .declinationDegrees = 90.0}));
    EXPECT_FALSE(an::isSkyCoordinate({.rightAscensionDegrees = 360.0, .declinationDegrees = 0.0}));
    EXPECT_FALSE(an::isSkyCoordinate({.rightAscensionDegrees = -1.0, .declinationDegrees = 0.0}));
    EXPECT_FALSE(an::isSkyCoordinate({.rightAscensionDegrees = 10.0, .declinationDegrees = 90.5}));
    EXPECT_FALSE(an::isSkyCoordinate(
        {.rightAscensionDegrees = std::numeric_limits<double>::infinity(), .declinationDegrees = 0.0}));
    EXPECT_FALSE(an::isSkyCoordinate(
        {.rightAscensionDegrees = 0.0, .declinationDegrees = std::numeric_limits<double>::quiet_NaN()}));
}

TEST(CatalogRecordTest, SeparationMatchesClosedFormAngles)
{
    const an::SkyCoordinate origin{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0};
    EXPECT_DOUBLE_EQ(an::angularSeparationDegrees(origin, origin), 0.0);

    // One degree along the equator, and the same degree in declination.
    EXPECT_NEAR(an::angularSeparationDegrees(origin, {.rightAscensionDegrees = 1.0, .declinationDegrees = 0.0}), 1.0,
                1.0e-12);
    EXPECT_NEAR(an::angularSeparationDegrees(origin, {.rightAscensionDegrees = 0.0, .declinationDegrees = 1.0}), 1.0,
                1.0e-12);

    // The wrap is an angle like any other: one degree short of a turn is one degree away.
    EXPECT_NEAR(an::angularSeparationDegrees(origin, {.rightAscensionDegrees = 359.0, .declinationDegrees = 0.0}), 1.0,
                1.0e-12);

    // Pole to pole, and a full antipode across the equator.
    EXPECT_NEAR(an::angularSeparationDegrees({.rightAscensionDegrees = 12.0, .declinationDegrees = 90.0},
                                             {.rightAscensionDegrees = 200.0, .declinationDegrees = -90.0}),
                180.0, 1.0e-9);
    EXPECT_NEAR(an::angularSeparationDegrees(origin, {.rightAscensionDegrees = 180.0, .declinationDegrees = 0.0}),
                180.0, 1.0e-9);

    // A degree of right ascension at sixty degrees of declination is half a degree of arc.
    EXPECT_NEAR(an::angularSeparationDegrees({.rightAscensionDegrees = 10.0, .declinationDegrees = 60.0},
                                             {.rightAscensionDegrees = 11.0, .declinationDegrees = 60.0}),
                0.5, 1.0e-4);
}

TEST(CatalogRecordTest, SeparationIsAccurateAtSmallAngles)
{
    // The separations a catalog match is made of. The cosine form loses most of its digits here;
    // the haversine form does not.
    const an::SkyCoordinate first{.rightAscensionDegrees = 83.822083, .declinationDegrees = -5.391111};
    const double offsetDegrees = 1.0 / 3600.0;
    const an::SkyCoordinate second{.rightAscensionDegrees = first.rightAscensionDegrees,
                                   .declinationDegrees = first.declinationDegrees + offsetDegrees};
    EXPECT_NEAR(an::angularSeparationDegrees(first, second), offsetDegrees, 1.0e-15);
}

TEST(CatalogRecordTest, SeparationOfANonDirectionIsNotANumber)
{
    const an::SkyCoordinate valid{.rightAscensionDegrees = 10.0, .declinationDegrees = 10.0};
    const an::SkyCoordinate broken{.rightAscensionDegrees = std::numeric_limits<double>::quiet_NaN(),
                                   .declinationDegrees = 0.0};
    EXPECT_TRUE(std::isnan(an::angularSeparationDegrees(valid, broken)));
}

TEST(CatalogRecordTest, DesignationGainsItsSeparatorAndCanonicalPrefix)
{
    EXPECT_EQ(an::normalizeCatalogDesignation("m83"), "M 83");
    EXPECT_EQ(an::normalizeCatalogDesignation("M83"), "M 83");
    EXPECT_EQ(an::normalizeCatalogDesignation("  m  83  "), "M 83");
    EXPECT_EQ(an::normalizeCatalogDesignation("ngc7000"), "NGC 7000");
    EXPECT_EQ(an::normalizeCatalogDesignation("IC1396"), "IC 1396");
    EXPECT_EQ(an::normalizeCatalogDesignation("ugc12345"), "UGC 12345");
    EXPECT_EQ(an::normalizeCatalogDesignation("pgc-3589"), "PGC 3589");
    EXPECT_EQ(an::normalizeCatalogDesignation("b33"), "B 33");
}

TEST(CatalogRecordTest, DesignationPrefersTheLongerCatalogPrefix)
{
    // Every one of these begins with a shorter prefix that is also in the table. The scan has to
    // reach the one whose remainder is a number.
    EXPECT_EQ(an::normalizeCatalogDesignation("mel20"), "Mel 20");
    EXPECT_EQ(an::normalizeCatalogDesignation("cr399"), "Cr 399");
    EXPECT_EQ(an::normalizeCatalogDesignation("ced214"), "Ced 214");
    EXPECT_EQ(an::normalizeCatalogDesignation("ldn1622"), "LDN 1622");
    EXPECT_EQ(an::normalizeCatalogDesignation("barnard33"), "Barnard 33");
    EXPECT_EQ(an::normalizeCatalogDesignation("BARNARD33"), "Barnard 33");
}

TEST(CatalogRecordTest, DesignationCarriesTheCatalogsOwnPunctuation)
{
    EXPECT_EQ(an::normalizeCatalogDesignation("sh2-240"), "Sh2-240");
    EXPECT_EQ(an::normalizeCatalogDesignation("SH2 240"), "Sh2-240");
    EXPECT_EQ(an::normalizeCatalogDesignation("caldwell14"), "C 14");
    EXPECT_EQ(an::normalizeCatalogDesignation("Caldwell 14"), "C 14");
    EXPECT_EQ(an::normalizeCatalogDesignation("c14"), "C 14");
}

TEST(CatalogRecordTest, DesignationKeepsWhatFollowsTheNumber)
{
    // What follows the number is carried through exactly as typed, case included.
    EXPECT_EQ(an::normalizeCatalogDesignation("ngc5195a"), "NGC 5195a");
    EXPECT_EQ(an::normalizeCatalogDesignation("NGC5195A"), "NGC 5195A");
    EXPECT_EQ(an::normalizeCatalogDesignation("m31 andromeda"), "M 31 andromeda");
}

TEST(CatalogRecordTest, TextThatIsNotADesignationIsReturnedUnchanged)
{
    EXPECT_EQ(an::normalizeCatalogDesignation(""), "");
    EXPECT_EQ(an::normalizeCatalogDesignation("Andromeda"), "Andromeda");
    EXPECT_EQ(an::normalizeCatalogDesignation("HIP 32349"), "HIP 32349");
    EXPECT_EQ(an::normalizeCatalogDesignation("M"), "M");
    EXPECT_EQ(an::normalizeCatalogDesignation("melbourne"), "melbourne");
    EXPECT_EQ(an::normalizeCatalogDesignation("north america nebula"), "north america nebula");

    // A long input that begins with a table prefix and never reaches a digit. The scan looks at the
    // prefix and the characters that could separate it from a number, and stops.
    const std::string long_text = "M" + std::string(100000u, 'x');
    EXPECT_EQ(an::normalizeCatalogDesignation(long_text), long_text);
}

TEST(CatalogRecordTest, SortDirectionMembershipIsClosed)
{
    EXPECT_TRUE(an::isCatalogSortDirection(an::CatalogSortDirection::Ascending));
    EXPECT_TRUE(an::isCatalogSortDirection(an::CatalogSortDirection::Descending));
    EXPECT_FALSE(an::isCatalogSortDirection(static_cast<an::CatalogSortDirection>(7)));
}

} // namespace
