// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "app/BundledCatalogs.h"
#include "controls/BrightStarAnnotationDisplayModule.h"

#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>

#include <algorithm>
#include <memory>
#include <string_view>

namespace
{

using an::tests::astrometry::DirectExecution;
using an::tests::astrometry::northUpPlane;
using an::tests::astrometry::openExecution;
using an::tests::astrometry::regionEntries;
using an::tests::astrometry::solutionOf;
using an::tests::astrometry::tangentPlaneCards;

/// The bright stars of a catalog text, built as the overlay builds its catalog; fails the case when they are refused.
std::optional<an::SkyCatalog> brightStars(const DirectExecution& direct, const std::string_view text)
{
    an::Result<an::SkyCatalog> catalog =
        an::ui::buildBundledCatalog(direct.execution(), an::ui::BundledCatalog::BrightStars, text);
    EXPECT_TRUE(catalog.hasValue()) << catalog.status().message();
    if (!catalog.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*catalog);
}

/// A region query as the overlay asks the bright stars: no star without a magnitude is admitted.
an::CatalogRegionQuery brightQuery(const double ra, const double dec, const double radius, const double limit)
{
    return an::ui::bundledCatalogRegionQuery(an::ui::BundledCatalog::BrightStars,
                                             an::SkyCoordinate{.rightAscensionDegrees = ra, .declinationDegrees = dec},
                                             radius, limit);
}

} // namespace

TEST(BrightStarAnnotationDisplayModule, BundledCatalogFileLoadsSuccessfully)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const an::Result<QByteArray> text =
        an::tests::astrometry::sourceTreeCatalogs()(an::ui::BundledCatalog::BrightStars);
    ASSERT_TRUE(text.hasValue()) << text.status().message();
    const std::optional<an::SkyCatalog> catalog =
        brightStars(*direct, std::string_view(text->constData(), static_cast<std::size_t>(text->size())));
    ASSERT_TRUE(catalog.has_value());
    EXPECT_GE(catalog->entryCount(), 8000u);

    const auto matches = regionEntries(direct->execution(), *catalog, brightQuery(83.75, -0.5, 15.0, 2.0));
    const auto contains_name = [&matches](const std::string& expected_name) {
        return std::any_of(matches.begin(), matches.end(), [&](const an::CatalogEntry* star) {
            return star != nullptr && star->commonName == expected_name;
        });
    };
    EXPECT_TRUE(contains_name("Betelgeuse"));
    EXPECT_TRUE(contains_name("Rigel"));
}

TEST(BrightStarAnnotationDisplayModule, QueryRegionFiltersByMagnitude)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const std::optional<an::SkyCatalog> catalog =
        brightStars(*direct, "id,common_name,bayer_designation,ra_deg,dec_deg,magnitude,spectral_type\n"
                             "HIP 1,Sirius,alf CMa,101.2872,-16.7161,-1.46,A1V\n"
                             "HIP 2,Dim Star,,101.5,-16.4,6.2,G2V\n");
    ASSERT_TRUE(catalog.has_value());

    EXPECT_EQ(regionEntries(direct->execution(), *catalog, brightQuery(101.3, -16.7, 2.0, 10.0)).size(), 2u);

    const auto bright_only = regionEntries(direct->execution(), *catalog, brightQuery(101.3, -16.7, 2.0, 2.0));
    ASSERT_EQ(bright_only.size(), 1u);
    EXPECT_EQ(bright_only.front()->commonName, "Sirius");
}

TEST(BrightStarAnnotationDisplayModule, QueryRegionNearRaWrapAround)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const std::optional<an::SkyCatalog> catalog =
        brightStars(*direct, "id,common_name,bayer_designation,ra_deg,dec_deg,magnitude,spectral_type\n"
                             "HIP 1,Wrap Star,alf Psc,359.5,0.0,2.0,A0V\n");
    ASSERT_TRUE(catalog.has_value());

    const auto matches = regionEntries(direct->execution(), *catalog, brightQuery(0.5, 0.0, 2.0, 6.5));
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches.front()->commonName, "Wrap Star");
}

TEST(BrightStarAnnotationDisplayModule, AStarWithNoMagnitudeIsNeverLabelledUnderALimit)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const std::optional<an::SkyCatalog> catalog =
        brightStars(*direct, "id,common_name,bayer_designation,ra_deg,dec_deg,magnitude,spectral_type\n"
                             "HIP 1,Named Unmeasured,,101.3,-16.7,99.0,A0V\n");
    ASSERT_TRUE(catalog.has_value());
    EXPECT_TRUE(regionEntries(direct->execution(), *catalog, brightQuery(101.3, -16.7, 2.0, 6.5)).empty());
}

TEST(BrightStarAnnotationDisplayModule, FormatLabelPrefersCommonNameThenBayerThenId)
{
    an::CatalogEntry common;
    common.identifier = "HIP 32349";
    common.commonName = "Sirius";
    common.designation = "alf CMa";
    EXPECT_EQ(an::ui::detail::formatBrightStarAnnotationLabel(common), QStringLiteral("Sirius"));

    an::CatalogEntry bayer_only;
    bayer_only.identifier = "HIP 27989";
    bayer_only.designation = "alf Ori";
    EXPECT_EQ(an::ui::detail::formatBrightStarAnnotationLabel(bayer_only), QStringLiteral("alf Ori"));

    an::CatalogEntry id_only;
    id_only.identifier = "HIP 12345";
    EXPECT_EQ(an::ui::detail::formatBrightStarAnnotationLabel(id_only), QStringLiteral("HIP 12345"));
}

TEST(BrightStarAnnotationDisplayModule, ProjectionMapsStarToTheCentreOfItsPixel)
{
    // CRPIX counts pixel centres from one, so (500, 400) is the engine's (499.5, 399.5): the centre of the pixel at
    // 499, 399.
    const auto cards = tangentPlaneCards(northUpPlane(500.0, 400.0, 101.2872, -16.7161, 0.01));
    const std::optional<an::WcsSolution> wcs = solutionOf(cards, 1000u, 800u);
    ASSERT_TRUE(wcs.has_value());

    an::CatalogEntry star;
    star.identifier = "HIP 32349";
    star.commonName = "Sirius";
    star.coordinate = {.rightAscensionDegrees = 101.2872, .declinationDegrees = -16.7161};

    const auto projected = an::ui::detail::projectBrightStarAnnotation(*wcs, star, 1.0);
    ASSERT_TRUE(projected.isValid());
    EXPECT_NEAR(projected.center.x(), 499.5, 1e-9);
    EXPECT_NEAR(projected.center.y(), 399.5, 1e-9);
}

TEST(BrightStarAnnotationDisplayModule, LoadFromCsvRejectsInvalidRows)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const an::Result<an::SkyCatalog> catalog =
        an::ui::buildBundledCatalog(direct->execution(), an::ui::BundledCatalog::BrightStars,
                                    "id,common_name,bayer_designation,ra_deg,dec_deg,magnitude,spectral_type\n"
                                    "HIP 1,Sirius,alf CMa,not_a_number,-16.7161,-1.46,A1V\n");
    ASSERT_FALSE(catalog.hasValue());
    EXPECT_FALSE(std::string(catalog.status().message()).empty());
}

TEST(BrightStarAnnotationDisplayModule, FormatLabelReturnsEmptyForStarWithAllBlankFields)
{
    an::CatalogEntry empty_star;
    EXPECT_TRUE(an::ui::detail::formatBrightStarAnnotationLabel(empty_star).isEmpty());

    an::CatalogEntry whitespace_star;
    whitespace_star.identifier = "   ";
    whitespace_star.commonName = "  ";
    whitespace_star.designation = " ";
    EXPECT_TRUE(an::ui::detail::formatBrightStarAnnotationLabel(whitespace_star).isEmpty());
}
