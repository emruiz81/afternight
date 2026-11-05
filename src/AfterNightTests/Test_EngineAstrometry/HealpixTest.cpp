// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "CatalogTestSupport.h"

#include <AfterNightEngine/Algorithms/Catalog/Healpix.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stop_token>
#include <vector>

namespace
{

using catalog_test::makeEngine;
using catalog_test::makeExecution;

[[nodiscard]] std::vector<std::uint32_t> valuesOf(const an::HealpixDiscResult& result)
{
    std::vector<std::uint32_t> values;
    values.reserve(result.pixels.size());
    for (const an::HealpixPixelIndex& pixel : result.pixels)
    {
        values.push_back(pixel.value);
    }
    return values;
}

[[nodiscard]] std::vector<std::uint32_t> bruteForceCentres(const an::HealpixGrid& grid, const an::SkyCoordinate& centre,
                                                           const double radiusDegrees)
{
    std::vector<std::uint32_t> values;
    for (std::uint32_t value = 0u; value < an::healpixPixelCount(grid); ++value)
    {
        const an::Result<an::SkyCoordinate> coordinate =
            an::healpixPixelCenter(an::HealpixPixelIndex{.grid = grid, .value = value});
        EXPECT_TRUE(coordinate.hasValue()) << coordinate.status().message();
        if (coordinate.hasValue() && an::angularSeparationDegrees(centre, *coordinate) <= radiusDegrees)
        {
            values.push_back(value);
        }
    }
    return values;
}

TEST(HealpixTest, TheGridCarriesOrderAndNumberingTogether)
{
    constexpr an::HealpixGrid lowest{.order = 0u, .ordering = an::HealpixOrdering::Ring};
    constexpr an::HealpixGrid catalog{.order = 5u, .ordering = an::HealpixOrdering::Nested};
    constexpr an::HealpixGrid highest{.order = 13u, .ordering = an::HealpixOrdering::Ring};
    constexpr an::HealpixGrid tooHigh{.order = 14u, .ordering = an::HealpixOrdering::Ring};
    constexpr an::HealpixGrid unknown{.order = 5u, .ordering = static_cast<an::HealpixOrdering>(9)};

    static_assert(an::isHealpixGrid(lowest));
    static_assert(an::healpixSideCount(catalog) == 32u);
    static_assert(an::healpixPixelCount(catalog) == 12288u);
    static_assert(an::healpixPixelCount(highest) == 805306368u);
    static_assert(!an::isHealpixGrid(tooHigh));
    static_assert(!an::isHealpixGrid(unknown));
    static_assert(an::healpixPixelCount(tooHigh) == 0u);
    static_assert(an::healpixPixelCount(unknown) == 0u);

    EXPECT_STREQ(an::healpixOrderingName(an::HealpixOrdering::Ring), "Ring");
    EXPECT_STREQ(an::healpixOrderingName(an::HealpixOrdering::Nested), "Nested");
    EXPECT_STREQ(an::healpixOrderingName(static_cast<an::HealpixOrdering>(9)), "UnknownHealpixOrdering");
}

TEST(HealpixTest, OrderZeroMatchesTheStandardRingAndFaceCentres)
{
    const an::HealpixGrid ring{.order = 0u, .ordering = an::HealpixOrdering::Ring};
    constexpr double kPolarDeclinationDegrees = 41.810314895778596;
    const std::array<an::SkyCoordinate, 12u> expected{
        an::SkyCoordinate{.rightAscensionDegrees = 45.0, .declinationDegrees = kPolarDeclinationDegrees},
        an::SkyCoordinate{.rightAscensionDegrees = 135.0, .declinationDegrees = kPolarDeclinationDegrees},
        an::SkyCoordinate{.rightAscensionDegrees = 225.0, .declinationDegrees = kPolarDeclinationDegrees},
        an::SkyCoordinate{.rightAscensionDegrees = 315.0, .declinationDegrees = kPolarDeclinationDegrees},
        an::SkyCoordinate{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0},
        an::SkyCoordinate{.rightAscensionDegrees = 90.0, .declinationDegrees = 0.0},
        an::SkyCoordinate{.rightAscensionDegrees = 180.0, .declinationDegrees = 0.0},
        an::SkyCoordinate{.rightAscensionDegrees = 270.0, .declinationDegrees = 0.0},
        an::SkyCoordinate{.rightAscensionDegrees = 45.0, .declinationDegrees = -kPolarDeclinationDegrees},
        an::SkyCoordinate{.rightAscensionDegrees = 135.0, .declinationDegrees = -kPolarDeclinationDegrees},
        an::SkyCoordinate{.rightAscensionDegrees = 225.0, .declinationDegrees = -kPolarDeclinationDegrees},
        an::SkyCoordinate{.rightAscensionDegrees = 315.0, .declinationDegrees = -kPolarDeclinationDegrees}};

    for (std::uint32_t value = 0u; value < expected.size(); ++value)
    {
        const an::Result<an::SkyCoordinate> centre =
            an::healpixPixelCenter(an::HealpixPixelIndex{.grid = ring, .value = value});
        ASSERT_TRUE(centre.hasValue()) << centre.status().message();
        EXPECT_NEAR(centre->rightAscensionDegrees, expected[value].rightAscensionDegrees, 1.0e-13);
        EXPECT_NEAR(centre->declinationDegrees, expected[value].declinationDegrees, 1.0e-13);
    }

    EXPECT_EQ(an::skyCoordinateToHealpixPixel(
                  ring, an::SkyCoordinate{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0})
                  ->value,
              4u);
    EXPECT_EQ(an::skyCoordinateToHealpixPixel(
                  ring, an::SkyCoordinate{.rightAscensionDegrees = 90.0, .declinationDegrees = 0.0})
                  ->value,
              5u);
}

TEST(HealpixTest, PublishedHealpixExampleCentresAreReproduced)
{
    const an::HealpixGrid ring{.order = 8u, .ordering = an::HealpixOrdering::Ring};
    const an::Result<an::SkyCoordinate> seventeen =
        an::healpixPixelCenter(an::HealpixPixelIndex{.grid = ring, .value = 17u});
    const an::Result<an::SkyCoordinate> thousand =
        an::healpixPixelCenter(an::HealpixPixelIndex{.grid = ring, .value = 1000u});
    ASSERT_TRUE(seventeen.hasValue()) << seventeen.status().message();
    ASSERT_TRUE(thousand.hasValue()) << thousand.status().message();

    const double thetaSeventeen = (90.0 - seventeen->declinationDegrees) * std::numbers::pi / 180.0;
    const double thetaThousand = (90.0 - thousand->declinationDegrees) * std::numbers::pi / 180.0;
    const double phiSeventeen = seventeen->rightAscensionDegrees * std::numbers::pi / 180.0;
    const double phiThousand = thousand->rightAscensionDegrees * std::numbers::pi / 180.0;
    EXPECT_NEAR(thetaSeventeen, 0.0095683558, 5.0e-11);
    EXPECT_NEAR(thetaThousand, 0.070182078, 5.0e-10);
    EXPECT_NEAR(phiSeventeen, 2.8797933, 5.0e-8);
    EXPECT_NEAR(phiThousand, 5.4620872, 5.0e-8);
}

TEST(HealpixTest, RingAndNestedAddressesMatchKnownStandardValues)
{
    const an::SkyCoordinate equator{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0};
    const an::HealpixGrid ring{.order = 1u, .ordering = an::HealpixOrdering::Ring};
    const an::HealpixGrid nested{.order = 1u, .ordering = an::HealpixOrdering::Nested};
    ASSERT_TRUE(an::skyCoordinateToHealpixPixel(ring, equator).hasValue());
    ASSERT_TRUE(an::skyCoordinateToHealpixPixel(nested, equator).hasValue());
    EXPECT_EQ(an::skyCoordinateToHealpixPixel(ring, equator)->value, 20u);
    EXPECT_EQ(an::skyCoordinateToHealpixPixel(nested, equator)->value, 17u);

    const an::SkyCoordinate north{.rightAscensionDegrees = 0.0, .declinationDegrees = 90.0};
    const an::SkyCoordinate south{.rightAscensionDegrees = 0.0, .declinationDegrees = -90.0};
    EXPECT_EQ(an::skyCoordinateToHealpixPixel(ring, north)->value, 0u);
    EXPECT_EQ(an::skyCoordinateToHealpixPixel(ring, south)->value, 44u);
}

TEST(HealpixTest, EveryLowOrderPixelCentreReturnsToItsOwnAddress)
{
    for (std::uint8_t order = 0u; order <= 5u; ++order)
    {
        for (const an::HealpixOrdering ordering : {an::HealpixOrdering::Ring, an::HealpixOrdering::Nested})
        {
            const an::HealpixGrid grid{.order = order, .ordering = ordering};
            for (std::uint32_t value = 0u; value < an::healpixPixelCount(grid); ++value)
            {
                const an::HealpixPixelIndex pixel{.grid = grid, .value = value};
                const an::Result<an::SkyCoordinate> centre = an::healpixPixelCenter(pixel);
                ASSERT_TRUE(centre.hasValue())
                    << "order " << static_cast<int>(order) << ", pixel " << value << ": " << centre.status().message();
                const an::Result<an::HealpixPixelIndex> returned = an::skyCoordinateToHealpixPixel(grid, *centre);
                ASSERT_TRUE(returned.hasValue()) << returned.status().message();
                EXPECT_EQ(*returned, pixel) << "order " << static_cast<int>(order) << ", pixel " << value;
            }
        }
    }
}

TEST(HealpixTest, TheMaximumPixelRadiusMatchesThePublishedReference)
{
    const an::Result<double> orderZero =
        an::healpixMaximumPixelRadiusDegrees(an::HealpixGrid{.order = 0u, .ordering = an::HealpixOrdering::Ring});
    const an::Result<double> orderFour =
        an::healpixMaximumPixelRadiusDegrees(an::HealpixGrid{.order = 4u, .ordering = an::HealpixOrdering::Nested});
    ASSERT_TRUE(orderZero.hasValue()) << orderZero.status().message();
    ASSERT_TRUE(orderFour.hasValue()) << orderFour.status().message();
    EXPECT_NEAR(*orderZero * std::numbers::pi / 180.0, 0.84106867056793, 1.0e-14);
    EXPECT_NEAR(*orderFour * std::numbers::pi / 180.0, 0.06601476143251, 1.0e-14);
}

TEST(HealpixTest, IndexOperationsRefuseUnsupportedGridsDirectionsAndAddresses)
{
    const an::HealpixGrid invalidOrder{.order = 14u, .ordering = an::HealpixOrdering::Ring};
    const an::HealpixGrid invalidOrdering{.order = 5u, .ordering = static_cast<an::HealpixOrdering>(9)};
    const an::HealpixGrid valid{.order = 2u, .ordering = an::HealpixOrdering::Ring};
    const an::SkyCoordinate direction{.rightAscensionDegrees = 12.0, .declinationDegrees = -34.0};

    EXPECT_EQ(an::skyCoordinateToHealpixPixel(invalidOrder, direction).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::skyCoordinateToHealpixPixel(invalidOrdering, direction).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::skyCoordinateToHealpixPixel(
                  valid, an::SkyCoordinate{.rightAscensionDegrees = 360.0, .declinationDegrees = 0.0})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::healpixPixelCenter(an::HealpixPixelIndex{.grid = valid, .value = an::healpixPixelCount(valid)})
                  .status()
                  .code(),
              an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::healpixMaximumPixelRadiusDegrees(invalidOrder).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(HealpixTest, SelectiveCentreQueriesEqualAnIndependentFullScan)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::HealpixGrid grid{.order = 4u, .ordering = an::HealpixOrdering::Ring};
    const std::array<an::HealpixDiscQuery, 5u> queries{
        an::HealpixDiscQuery{.center = {.rightAscensionDegrees = 120.0, .declinationDegrees = -35.0},
                             .radiusDegrees = 0.25,
                             .coverage = an::HealpixDiscCoverage::Centers},
        an::HealpixDiscQuery{.center = {.rightAscensionDegrees = 359.8, .declinationDegrees = 0.0},
                             .radiusDegrees = 12.0,
                             .coverage = an::HealpixDiscCoverage::Centers},
        an::HealpixDiscQuery{.center = {.rightAscensionDegrees = 15.0, .declinationDegrees = 89.0},
                             .radiusDegrees = 7.0,
                             .coverage = an::HealpixDiscCoverage::Centers},
        an::HealpixDiscQuery{.center = {.rightAscensionDegrees = 220.0, .declinationDegrees = -88.0},
                             .radiusDegrees = 20.0,
                             .coverage = an::HealpixDiscCoverage::Centers},
        an::HealpixDiscQuery{.center = {.rightAscensionDegrees = 42.0, .declinationDegrees = 12.0},
                             .radiusDegrees = 90.0,
                             .coverage = an::HealpixDiscCoverage::Centers}};

    for (const an::HealpixDiscQuery& query : queries)
    {
        const an::Result<an::HealpixDiscResult> selected = an::queryHealpixDisc(execution, grid, query);
        ASSERT_TRUE(selected.hasValue()) << selected.status().message();
        EXPECT_EQ(valuesOf(*selected), bruteForceCentres(grid, query.center, query.radiusDegrees));
    }
}

TEST(HealpixTest, APointQueryReturnsTheOneOwningPixel)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::HealpixGrid grid{.order = 5u, .ordering = an::HealpixOrdering::Ring};
    an::HealpixDiscQuery query;
    query.center = an::SkyCoordinate{.rightAscensionDegrees = 120.0, .declinationDegrees = -35.0};
    query.radiusDegrees = 0.0;
    query.coverage = an::HealpixDiscCoverage::ConservativeOverlap;

    const an::Result<an::HealpixDiscResult> selected = an::queryHealpixDisc(execution, grid, query);
    const an::Result<an::HealpixPixelIndex> owning = an::skyCoordinateToHealpixPixel(grid, query.center);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    ASSERT_TRUE(owning.hasValue()) << owning.status().message();
    ASSERT_EQ(selected->pixels.size(), 1u);
    EXPECT_EQ(selected->pixels.front(), *owning);
    EXPECT_EQ(selected->candidateCentersExamined, 1u);
}

TEST(HealpixTest, ConservativeQueriesCannotLoseAPointInsideTheCone)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::HealpixGrid grid{.order = 5u, .ordering = an::HealpixOrdering::Ring};
    an::HealpixDiscQuery query;
    query.center = an::SkyCoordinate{.rightAscensionDegrees = 120.0, .declinationDegrees = -35.0};
    query.radiusDegrees = 2.0;
    query.coverage = an::HealpixDiscCoverage::ConservativeOverlap;
    const an::Result<an::HealpixDiscResult> selected = an::queryHealpixDisc(execution, grid, query);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    const std::vector<std::uint32_t> selectedValues = valuesOf(*selected);

    for (double declination = -38.0; declination <= -32.0; declination += 0.125)
    {
        for (double rightAscension = 116.0; rightAscension <= 124.0; rightAscension += 0.125)
        {
            const an::SkyCoordinate sample{.rightAscensionDegrees = rightAscension, .declinationDegrees = declination};
            if (an::angularSeparationDegrees(query.center, sample) > query.radiusDegrees)
            {
                continue;
            }
            const an::Result<an::HealpixPixelIndex> pixel = an::skyCoordinateToHealpixPixel(grid, sample);
            ASSERT_TRUE(pixel.hasValue()) << pixel.status().message();
            EXPECT_TRUE(std::binary_search(selectedValues.begin(), selectedValues.end(), pixel->value));
        }
    }
}

TEST(HealpixTest, RingAndNestedQueriesSelectTheSamePhysicalPixels)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::HealpixGrid ring{.order = 5u, .ordering = an::HealpixOrdering::Ring};
    const an::HealpixGrid nested{.order = 5u, .ordering = an::HealpixOrdering::Nested};
    an::HealpixDiscQuery query;
    query.center = an::SkyCoordinate{.rightAscensionDegrees = 359.0, .declinationDegrees = 65.0};
    query.radiusDegrees = 18.0;
    query.coverage = an::HealpixDiscCoverage::Centers;

    const an::Result<an::HealpixDiscResult> ringResult = an::queryHealpixDisc(execution, ring, query);
    const an::Result<an::HealpixDiscResult> nestedResult = an::queryHealpixDisc(execution, nested, query);
    ASSERT_TRUE(ringResult.hasValue()) << ringResult.status().message();
    ASSERT_TRUE(nestedResult.hasValue()) << nestedResult.status().message();

    std::vector<std::uint32_t> converted;
    converted.reserve(ringResult->pixels.size());
    for (const an::HealpixPixelIndex& ringPixel : ringResult->pixels)
    {
        const an::Result<an::SkyCoordinate> centre = an::healpixPixelCenter(ringPixel);
        ASSERT_TRUE(centre.hasValue()) << centre.status().message();
        const an::Result<an::HealpixPixelIndex> nestedPixel = an::skyCoordinateToHealpixPixel(nested, *centre);
        ASSERT_TRUE(nestedPixel.hasValue()) << nestedPixel.status().message();
        converted.push_back(nestedPixel->value);
    }
    std::sort(converted.begin(), converted.end());
    EXPECT_EQ(converted, valuesOf(*nestedResult));
}

TEST(HealpixTest, AHighOrderNarrowQueryDoesNotScanTheSky)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::HealpixGrid grid{.order = 13u, .ordering = an::HealpixOrdering::Ring};
    an::HealpixDiscQuery query;
    query.center = an::SkyCoordinate{.rightAscensionDegrees = 123.456, .declinationDegrees = -32.1};
    query.radiusDegrees = 0.01;
    query.coverage = an::HealpixDiscCoverage::Centers;

    const an::Result<an::HealpixDiscResult> selected = an::queryHealpixDisc(execution, grid, query);
    ASSERT_TRUE(selected.hasValue()) << selected.status().message();
    EXPECT_FALSE(selected->pixels.empty());
    EXPECT_EQ(selected->ringsExamined, 4u * an::healpixSideCount(grid) - 1u);
    EXPECT_LT(selected->candidateCentersExamined, 256u);
    EXPECT_LT(selected->candidateCentersExamined, static_cast<std::uint64_t>(an::healpixPixelCount(grid)) / 1000000u);
}

TEST(HealpixTest, WholeSkyResultsAreAscendingAndBounded)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::HealpixGrid grid{.order = 1u, .ordering = an::HealpixOrdering::Nested};
    an::HealpixDiscQuery query;
    query.center = an::SkyCoordinate{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0};
    query.radiusDegrees = 180.0;
    query.coverage = an::HealpixDiscCoverage::Centers;
    query.maximumPixels = an::healpixPixelCount(grid);

    const an::Result<an::HealpixDiscResult> all = an::queryHealpixDisc(execution, grid, query);
    ASSERT_TRUE(all.hasValue()) << all.status().message();
    ASSERT_EQ(all->pixels.size(), an::healpixPixelCount(grid));
    for (std::uint32_t index = 0u; index < all->pixels.size(); ++index)
    {
        EXPECT_EQ(all->pixels[index].value, index);
        EXPECT_EQ(all->pixels[index].grid, grid);
    }

    --query.maximumPixels;
    const an::Status refused = an::queryHealpixDisc(execution, grid, query).status();
    EXPECT_EQ(refused.code(), an::ErrorCode::LimitExceeded);
    ASSERT_NE(refused.findContext(an::ContextKey::Requested), nullptr);
    ASSERT_NE(refused.findContext(an::ContextKey::Limit), nullptr);
    EXPECT_EQ(refused.findContext(an::ContextKey::Requested)->count, an::healpixPixelCount(grid));
    EXPECT_EQ(refused.findContext(an::ContextKey::Limit)->count, an::healpixPixelCount(grid) - 1u);
}

TEST(HealpixTest, ConeQueriesReturnEveryInvalidInputAndCancellation)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::HealpixGrid valid{.order = 5u, .ordering = an::HealpixOrdering::Ring};
    an::HealpixDiscQuery query;
    query.center = an::SkyCoordinate{.rightAscensionDegrees = 12.0, .declinationDegrees = -34.0};

    const an::ExecutionContext none;
    EXPECT_EQ(an::queryHealpixDisc(none, valid, query).status().code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(
        an::queryHealpixDisc(execution, an::HealpixGrid{.order = 14u, .ordering = an::HealpixOrdering::Ring}, query)
            .status()
            .code(),
        an::ErrorCode::InvalidArgument);

    query.center.rightAscensionDegrees = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::queryHealpixDisc(execution, valid, query).status().code(), an::ErrorCode::InvalidArgument);
    query.center = an::SkyCoordinate{.rightAscensionDegrees = 12.0, .declinationDegrees = -34.0};
    query.radiusDegrees = -1.0;
    EXPECT_EQ(an::queryHealpixDisc(execution, valid, query).status().code(), an::ErrorCode::InvalidArgument);
    query.radiusDegrees = 181.0;
    EXPECT_EQ(an::queryHealpixDisc(execution, valid, query).status().code(), an::ErrorCode::InvalidArgument);
    query.radiusDegrees = 1.0;
    query.coverage = static_cast<an::HealpixDiscCoverage>(9);
    EXPECT_EQ(an::queryHealpixDisc(execution, valid, query).status().code(), an::ErrorCode::InvalidArgument);
    query.coverage = an::HealpixDiscCoverage::Centers;
    query.maximumPixels = 0u;
    EXPECT_EQ(an::queryHealpixDisc(execution, valid, query).status().code(), an::ErrorCode::InvalidArgument);

    std::stop_source stop;
    stop.request_stop();
    const an::ExecutionContext cancelled = makeExecution(engine, 4u, stop.get_token());
    query.maximumPixels = 100u;
    EXPECT_EQ(an::queryHealpixDisc(cancelled, valid, query).status().code(), an::ErrorCode::Cancelled);
}

} // namespace
