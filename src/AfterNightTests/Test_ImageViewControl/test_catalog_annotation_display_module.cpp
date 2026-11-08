// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "app/BundledCatalogs.h"
#include "controls/CatalogAnnotationDisplayModule.h"

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <string_view>

namespace
{

using an::tests::astrometry::DirectExecution;
using an::tests::astrometry::northUpPlane;
using an::tests::astrometry::openExecution;
using an::tests::astrometry::regionEntries;
using an::tests::astrometry::solutionOf;
using an::tests::astrometry::tangentPlaneCards;

constexpr std::string_view kHeader =
    "id,common_name,ra_deg,dec_deg,major_axis_arcmin,minor_axis_arcmin,position_angle_deg,object_type,magnitude\n";

/// The deep-sky objects of a catalog text, built as the overlay builds its catalog; fails the case when they are
/// refused.
std::optional<an::SkyCatalog> deepSky(const DirectExecution& direct, const std::string& rows)
{
    const std::string text = std::string(kHeader) + rows;
    an::Result<an::SkyCatalog> catalog =
        an::ui::buildBundledCatalog(direct.execution(), an::ui::BundledCatalog::DeepSky, text);
    EXPECT_TRUE(catalog.hasValue()) << catalog.status().message();
    if (!catalog.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*catalog);
}

/// A region query as the overlay asks the deep-sky objects: a named object nobody measured is admitted.
an::CatalogRegionQuery deepQuery(const double ra, const double dec, const double radius, const double limit = 99.0)
{
    return an::ui::bundledCatalogRegionQuery(an::ui::BundledCatalog::DeepSky,
                                             an::SkyCoordinate{.rightAscensionDegrees = ra, .declinationDegrees = dec},
                                             radius, limit);
}

/// How round an outline is: its nearest point to the centre over its farthest, 1 for a circle.
double outlineRoundness(const an::ui::CatalogAnnotationProjectedShape& shape)
{
    double nearest = std::numeric_limits<double>::infinity();
    double farthest = 0.0;
    for (const QPointF& point : shape.outline)
    {
        const double radius = std::hypot(point.x() - shape.center.x(), point.y() - shape.center.y());
        nearest = (std::min)(nearest, radius);
        farthest = (std::max)(farthest, radius);
    }
    return farthest > 0.0 ? nearest / farthest : 0.0;
}

/// The direction on the sky, in degrees east of north and folded into [0, 180), of an outline's point farthest from its
/// centre: the long axis the drawing gives the object.
double outlineLongAxisDegrees(const an::WcsSolution& solution, const an::ui::CatalogAnnotationProjectedShape& shape)
{
    const auto farthest = std::max_element(
        shape.outline.cbegin(), shape.outline.cend(), [&shape](const QPointF& left, const QPointF& right) {
            return std::hypot(left.x() - shape.center.x(), left.y() - shape.center.y()) <
                   std::hypot(right.x() - shape.center.x(), right.y() - shape.center.y());
        });
    const an::Result<an::SkyCoordinate> centre =
        solution.pixelToWorld(an::PixelCoordinate{.x = shape.center.x(), .y = shape.center.y()});
    const an::Result<an::SkyCoordinate> tip =
        solution.pixelToWorld(an::PixelCoordinate{.x = farthest->x(), .y = farthest->y()});
    EXPECT_TRUE(centre.hasValue() && tip.hasValue());
    if (!centre.hasValue() || !tip.hasValue())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double cosDec = std::cos(centre->declinationDegrees * std::numbers::pi / 180.0);
    const double east = (tip->rightAscensionDegrees - centre->rightAscensionDegrees) * cosDec;
    const double north = tip->declinationDegrees - centre->declinationDegrees;
    const double degrees = std::atan2(east, north) * 180.0 / std::numbers::pi;
    return std::fmod(degrees + 360.0, 180.0);
}

} // namespace

TEST(CatalogAnnotationDisplayModule, BundledCatalogFileLoadsSuccessfully)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const an::Result<QByteArray> text = an::tests::astrometry::sourceTreeCatalogs()(an::ui::BundledCatalog::DeepSky);
    ASSERT_TRUE(text.hasValue()) << text.status().message();
    const an::Result<an::SkyCatalog> catalog =
        an::ui::buildBundledCatalog(direct->execution(), an::ui::BundledCatalog::DeepSky,
                                    std::string_view(text->constData(), static_cast<std::size_t>(text->size())));
    ASSERT_TRUE(catalog.hasValue()) << catalog.status().message();
    EXPECT_GE(catalog->entryCount(), 20u);

    const auto matches = regionEntries(direct->execution(), *catalog, deepQuery(10.6847, 41.2692, 1.0, 10.0));
    ASSERT_FALSE(matches.empty());
    EXPECT_EQ(matches.front()->identifier, "M 31");
}

TEST(CatalogAnnotationDisplayModule, AnnotationProjectsObjectToTheCentreOfItsPixel)
{
    // CRPIX (500, 400) counts pixel centres from one: the engine's (499.5, 399.5).
    const std::optional<an::WcsSolution> wcs =
        solutionOf(tangentPlaneCards(northUpPlane(500.0, 400.0, 10.6847, 41.2692, 0.01)), 1000u, 800u);
    ASSERT_TRUE(wcs.has_value());

    an::CatalogEntry object;
    object.identifier = "M 31";
    object.commonName = "Andromeda Galaxy";
    object.coordinate = {.rightAscensionDegrees = 10.6847, .declinationDegrees = 41.2692};
    object.extent =
        an::CatalogExtent{.majorAxisArcminutes = 190.0, .minorAxisArcminutes = 60.0, .positionAngleDegrees = 35.0};
    object.objectType = "Galaxy";
    object.magnitude = 3.4;

    const an::ui::CatalogAnnotationProjectedShape shape =
        an::ui::detail::projectCatalogObjectAnnotation(*wcs, object, 1.0);
    EXPECT_NEAR(shape.center.x(), 499.5, 1e-9);
    EXPECT_NEAR(shape.center.y(), 399.5, 1e-9);
    EXPECT_GE(shape.outline.size(), 16);
}

TEST(CatalogAnnotationDisplayModule, AnOutlinesLongAxisLiesAtItsPositionAngleEastOfNorth)
{
    // A north-up field and one turned by 30 degrees: the long axis is read back on the sky, whatever way the image
    // holds it.
    const double scale = 0.0005;
    const double turn = 30.0 * std::numbers::pi / 180.0;
    const an::tests::astrometry::TangentPlane northUp = northUpPlane(500.0, 400.0, 204.25, -29.8667, scale);
    an::tests::astrometry::TangentPlane turned = northUp;
    turned.cd11 = -scale * std::cos(turn);
    turned.cd12 = scale * std::sin(turn);
    turned.cd21 = scale * std::sin(turn);
    turned.cd22 = scale * std::cos(turn);
    for (const an::tests::astrometry::TangentPlane& plane : {northUp, turned})
    {
        const std::optional<an::WcsSolution> wcs = solutionOf(tangentPlaneCards(plane), 1000u, 800u);
        ASSERT_TRUE(wcs.has_value());
        for (const double positionAngle : {0.0, 45.0, 120.0})
        {
            an::CatalogEntry object;
            object.identifier = "NGC 1";
            object.coordinate = {.rightAscensionDegrees = 204.25, .declinationDegrees = -29.8667};
            object.extent = an::CatalogExtent{
                .majorAxisArcminutes = 10.0, .minorAxisArcminutes = 5.0, .positionAngleDegrees = positionAngle};

            const an::ui::CatalogAnnotationProjectedShape shape =
                an::ui::detail::projectCatalogObjectAnnotation(*wcs, object, 1.0);
            ASSERT_GE(shape.outline.size(), 16);
            EXPECT_NEAR(outlineRoundness(shape), 0.5, 0.02);
            const double drawn = outlineLongAxisDegrees(*wcs, shape);
            const double apart = std::fabs(drawn - positionAngle);
            EXPECT_LT((std::min)(apart, 180.0 - apart), 1.0)
                << "position angle " << positionAngle << ", drawn at " << drawn;
        }
    }
}

TEST(CatalogAnnotationDisplayModule, ABundledObjectWhoseCatalogStatesOnlyADiameterIsDrawnRound)
{
    // NGC 2000.0 gives M 83 one size, so the catalog knows no axis ratio or orientation for it, and its outline is a
    // circle of that size; a galaxy whose axes the catalog states keeps them.
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const an::Result<QByteArray> text = an::tests::astrometry::sourceTreeCatalogs()(an::ui::BundledCatalog::DeepSky);
    ASSERT_TRUE(text.hasValue()) << text.status().message();
    const an::Result<an::SkyCatalog> catalog =
        an::ui::buildBundledCatalog(direct->execution(), an::ui::BundledCatalog::DeepSky,
                                    std::string_view(text->constData(), static_cast<std::size_t>(text->size())), false);
    ASSERT_TRUE(catalog.hasValue()) << catalog.status().message();
    const auto named = [&catalog](const std::string_view identifier) -> const an::CatalogEntry* {
        for (std::uint32_t index = 0; index < catalog->entryCount(); ++index)
        {
            const an::CatalogEntry* entry = catalog->entry(index);
            if (entry != nullptr && entry->identifier == identifier)
            {
                return entry;
            }
        }
        return nullptr;
    };

    for (const std::string_view identifier : {std::string_view("M 83"), std::string_view("NGC 5236")})
    {
        const an::CatalogEntry* m83 = named(identifier);
        ASSERT_NE(m83, nullptr) << identifier;
        const std::optional<an::WcsSolution> wcs =
            solutionOf(tangentPlaneCards(northUpPlane(500.0, 400.0, m83->coordinate.rightAscensionDegrees,
                                                      m83->coordinate.declinationDegrees, 0.0005)),
                       1000u, 800u);
        ASSERT_TRUE(wcs.has_value());
        const an::ui::CatalogAnnotationProjectedShape shape =
            an::ui::detail::projectCatalogObjectAnnotation(*wcs, *m83, 1.0);
        ASSERT_GE(shape.outline.size(), 16) << identifier;
        EXPECT_GT(outlineRoundness(shape), 0.98) << identifier;
    }

    const an::CatalogEntry* m51 = named("M 51");
    ASSERT_NE(m51, nullptr);
    const std::optional<an::WcsSolution> wcs =
        solutionOf(tangentPlaneCards(northUpPlane(500.0, 400.0, m51->coordinate.rightAscensionDegrees,
                                                  m51->coordinate.declinationDegrees, 0.0005)),
                   1000u, 800u);
    ASSERT_TRUE(wcs.has_value());
    const an::ui::CatalogAnnotationProjectedShape shape =
        an::ui::detail::projectCatalogObjectAnnotation(*wcs, *m51, 1.0);
    EXPECT_NEAR(outlineRoundness(shape), 6.9 / 11.2, 0.02);
}

TEST(CatalogAnnotationDisplayModule, ConeSearchExcludesDistantObjects)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const std::optional<an::SkyCatalog> catalog = deepSky(*direct, "OBJ_NEAR,,10.0,41.0,5.0,5.0,0,Galaxy,8.0\n"
                                                                   "OBJ_FAR,,180.0,-30.0,5.0,5.0,0,Galaxy,8.0\n");
    ASSERT_TRUE(catalog.has_value());
    EXPECT_EQ(catalog->entryCount(), 2u);

    const auto matches = regionEntries(direct->execution(), *catalog, deepQuery(10.0, 41.0, 5.0));
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches.front()->identifier, "OBJ_NEAR");
}

TEST(CatalogAnnotationDisplayModule, QueryRegionFiltersByMagnitude)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const std::optional<an::SkyCatalog> catalog = deepSky(*direct, "BRIGHT,,10.0,41.0,5.0,5.0,0,Galaxy,6.0\n"
                                                                   "DIM,,10.1,41.1,5.0,5.0,0,Galaxy,14.0\n");
    ASSERT_TRUE(catalog.has_value());

    EXPECT_EQ(regionEntries(direct->execution(), *catalog, deepQuery(10.0, 41.0, 5.0, 20.0)).size(), 2u);

    const auto bright_only = regionEntries(direct->execution(), *catalog, deepQuery(10.0, 41.0, 5.0, 10.0));
    ASSERT_EQ(bright_only.size(), 1u);
    EXPECT_EQ(bright_only.front()->identifier, "BRIGHT");
}

TEST(CatalogAnnotationDisplayModule, QueryRegionNearRaWrapAround)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const std::optional<an::SkyCatalog> catalog = deepSky(*direct, "WRAP_OBJ,,359.5,0.0,1.0,1.0,0,Galaxy,8.0\n");
    ASSERT_TRUE(catalog.has_value());

    const auto matches = regionEntries(direct->execution(), *catalog, deepQuery(0.5, 0.0, 2.0));
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches.front()->identifier, "WRAP_OBJ");
}

TEST(CatalogAnnotationDisplayModule, QueryRegionNearPole)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const std::optional<an::SkyCatalog> catalog = deepSky(*direct, "POLE_OBJ,,45.0,89.5,1.0,1.0,0,Galaxy,8.0\n");
    ASSERT_TRUE(catalog.has_value());

    const auto matches = regionEntries(direct->execution(), *catalog, deepQuery(180.0, 89.0, 2.0));
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_EQ(matches.front()->identifier, "POLE_OBJ");
}

TEST(CatalogAnnotationDisplayModule, LoadFromCsvRejectsInvalidRows)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const an::Result<an::SkyCatalog> catalog =
        an::ui::buildBundledCatalog(direct->execution(), an::ui::BundledCatalog::DeepSky,
                                    std::string(kHeader) + "BAD_OBJ,,not_a_number,41.0,5.0,5.0,0,Galaxy,8.0\n");
    ASSERT_FALSE(catalog.hasValue());
    EXPECT_FALSE(std::string(catalog.status().message()).empty());
}

TEST(CatalogAnnotationDisplayModule, LoadFromCsvRejectsEmptyId)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const an::Result<an::SkyCatalog> catalog =
        an::ui::buildBundledCatalog(direct->execution(), an::ui::BundledCatalog::DeepSky,
                                    std::string(kHeader) + ",,10.0,41.0,5.0,5.0,0,Galaxy,8.0\n");
    ASSERT_FALSE(catalog.hasValue());
    EXPECT_FALSE(std::string(catalog.status().message()).empty());
}

TEST(CatalogAnnotationDisplayModule, LoadFromCsvRejectsEmptyContent)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    EXPECT_FALSE(an::ui::buildBundledCatalog(direct->execution(), an::ui::BundledCatalog::DeepSky, std::string_view())
                     .hasValue());
}

TEST(CatalogAnnotationDisplayModule, NormalizeRaDegWrapsNegativeValues)
{
    EXPECT_NEAR(an::normalizeRightAscensionDegrees(-10.0), 350.0, 1e-9);
    EXPECT_NEAR(an::normalizeRightAscensionDegrees(370.0), 10.0, 1e-9);
    EXPECT_NEAR(an::normalizeRightAscensionDegrees(0.0), 0.0, 1e-9);
    EXPECT_NEAR(an::normalizeRightAscensionDegrees(180.0), 180.0, 1e-9);
}

TEST(CatalogAnnotationDisplayModule, AngularSeparationDegIsSymmetric)
{
    const an::SkyCoordinate first{.rightAscensionDegrees = 10.0, .declinationDegrees = 41.0};
    const an::SkyCoordinate second{.rightAscensionDegrees = 20.0, .declinationDegrees = 45.0};
    const double sep1 = an::angularSeparationDegrees(first, second);
    const double sep2 = an::angularSeparationDegrees(second, first);
    EXPECT_NEAR(sep1, sep2, 1e-9);
    EXPECT_GT(sep1, 0.0);
}

TEST(CatalogAnnotationDisplayModule, QueryRegionRejectsInvalidRadius)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const std::optional<an::SkyCatalog> catalog = deepSky(*direct, "OBJ,,10.0,41.0,5.0,5.0,0,Galaxy,8.0\n");
    ASSERT_TRUE(catalog.has_value());

    // A radius that is not positive is a refused question, not an empty answer.
    EXPECT_FALSE(an::queryCatalogRegion(direct->execution(), *catalog, deepQuery(10.0, 41.0, 0.0)).hasValue());
    EXPECT_FALSE(an::queryCatalogRegion(direct->execution(), *catalog, deepQuery(10.0, 41.0, -1.0)).hasValue());
}

TEST(CatalogAnnotationDisplayModule, QueryRegionIncludesNamedObjectsWithUnknownMagnitude)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const std::optional<an::SkyCatalog> catalog =
        deepSky(*direct, "NGC 3372,Carina Nebula,160.95,-59.8667,120.0,120.0,0,Bright Nebula,99.0\n"
                         "NGC 9999,,160.0,-59.0,1.0,1.0,0,Galaxy,99.0\n"
                         "NGC 3293,,158.95,-58.2333,40.0,40.0,0,Cluster with Nebulosity,4.7\n");
    ASSERT_TRUE(catalog.has_value());

    const auto matches = regionEntries(direct->execution(), *catalog, deepQuery(160.0, -59.0, 5.0, 12.0));
    // NGC 3293 passes the magnitude limit. NGC 3372 has no measured magnitude but a common name, so it is admitted;
    // NGC 9999 has neither and is not.
    ASSERT_EQ(matches.size(), 2u);

    bool found_carina = false;
    bool found_ngc3293 = false;
    for (const an::CatalogEntry* obj : matches)
    {
        found_carina = found_carina || obj->identifier == "NGC 3372";
        found_ngc3293 = found_ngc3293 || obj->identifier == "NGC 3293";
    }
    EXPECT_TRUE(found_carina) << "Carina Nebula (named, no magnitude) should be included";
    EXPECT_TRUE(found_ngc3293) << "NGC 3293 (mag 4.7) should be included";
}

TEST(CatalogAnnotationDisplayModule, QueryRegionFiltersNamedObjectsWithKnownMagnitude)
{
    std::unique_ptr<DirectExecution> direct = openExecution();
    ASSERT_NE(direct, nullptr);
    const std::optional<an::SkyCatalog> catalog =
        deepSky(*direct, "C 2,Bow-Tie Nebula,3.25,72.52,1.0,1.0,0,Planetary Nebula,11.4\n");
    ASSERT_TRUE(catalog.has_value());

    EXPECT_TRUE(regionEntries(direct->execution(), *catalog, deepQuery(3.25, 72.52, 1.0, 6.0)).empty());
    EXPECT_EQ(regionEntries(direct->execution(), *catalog, deepQuery(3.25, 72.52, 1.0, 12.0)).size(), 1u);
}
