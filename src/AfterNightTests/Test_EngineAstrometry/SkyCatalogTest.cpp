// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The owned catalog: what it refuses to hold, and the region and name answers it gives.

#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>

#include "CatalogTestSupport.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace
{

using catalog_test::buildOrFail;
using catalog_test::identifiersOf;
using catalog_test::makeEngine;
using catalog_test::makeEntry;
using catalog_test::makeExecution;

/// A small field around one point, plus entries far away from it.
[[nodiscard]] std::vector<an::CatalogEntry> sampleEntries()
{
    std::vector<an::CatalogEntry> entries;
    entries.push_back(makeEntry("NGC 7000", 314.75, 44.3167, 4.0, "North America Nebula"));
    entries.push_back(makeEntry("IC 5070", 314.70, 44.3600, 8.0, "Pelican Nebula"));
    entries.push_back(makeEntry("NGC 6997", 314.55, 44.6333, 10.0));
    entries.push_back(makeEntry("M 31", 10.6847, 41.2687, 3.4, "Andromeda Galaxy"));
    entries.push_back(makeEntry("M 42", 83.8221, -5.3911, 4.0, "Orion Nebula"));
    entries.push_back(makeEntry("Barnard 33", 85.2458, -2.4589, {}, "Horsehead Nebula"));
    entries.push_back(makeEntry("PGC 9999", 85.2500, -2.4600, {}));
    return entries;
}

TEST(SkyCatalogTest, AnEmptyHandleAnswersEverySafely)
{
    const an::SkyCatalog empty;
    EXPECT_FALSE(empty.valid());
    EXPECT_EQ(empty.entryCount(), 0u);
    EXPECT_TRUE(empty.entries().empty());
    EXPECT_EQ(empty.entry(0u), nullptr);
    EXPECT_TRUE(empty.photometricBand().empty());
    EXPECT_FALSE(empty.hasNameIndex());
}

TEST(SkyCatalogTest, CopiesShareOneImmutableTable)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::SkyCatalogRequest request;
    request.photometricBand = "V";
    const an::SkyCatalog catalog = buildOrFail(execution, sampleEntries(), request);

    const an::SkyCatalog copy = catalog;
    EXPECT_TRUE(copy.valid());
    EXPECT_EQ(copy.entries().data(), catalog.entries().data());
    EXPECT_EQ(copy.photometricBand(), "V");
    EXPECT_TRUE(copy.hasNameIndex());

    an::SkyCatalog moved = std::move(copy);
    EXPECT_EQ(moved.entryCount(), catalog.entryCount());
}

TEST(SkyCatalogTest, BuildingRefusesAnEntryThatIsNotOne)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    std::vector<an::CatalogEntry> noIdentifier = sampleEntries();
    noIdentifier[2].identifier.clear();
    EXPECT_EQ(an::buildSkyCatalog(execution, noIdentifier).status().code(), an::ErrorCode::InvalidData);

    std::vector<an::CatalogEntry> badDeclination = sampleEntries();
    badDeclination[1].coordinate.declinationDegrees = 91.0;
    EXPECT_EQ(an::buildSkyCatalog(execution, badDeclination).status().code(), an::ErrorCode::InvalidData);

    std::vector<an::CatalogEntry> unfoldedRightAscension = sampleEntries();
    unfoldedRightAscension[1].coordinate.rightAscensionDegrees = 400.0;
    EXPECT_EQ(an::buildSkyCatalog(execution, unfoldedRightAscension).status().code(), an::ErrorCode::InvalidData);

    std::vector<an::CatalogEntry> badMagnitude = sampleEntries();
    badMagnitude[0].magnitude = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::buildSkyCatalog(execution, badMagnitude).status().code(), an::ErrorCode::InvalidData);

    std::vector<an::CatalogEntry> badExtent = sampleEntries();
    badExtent[0].extent = an::CatalogExtent{.majorAxisArcminutes = -1.0};
    EXPECT_EQ(an::buildSkyCatalog(execution, badExtent).status().code(), an::ErrorCode::InvalidData);

    EXPECT_EQ(an::buildSkyCatalog(execution, {}).status().code(), an::ErrorCode::EmptySelection);

    const an::ExecutionContext nothing;
    EXPECT_EQ(an::buildSkyCatalog(nothing, sampleEntries()).status().code(), an::ErrorCode::NotInitialized);
}

TEST(SkyCatalogTest, EntriesKeepTheOrderTheyWereGiven)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<an::CatalogEntry> source = sampleEntries();
    const an::SkyCatalog catalog = buildOrFail(execution, source);

    ASSERT_EQ(catalog.entryCount(), source.size());
    for (std::uint32_t index = 0u; index < catalog.entryCount(); ++index)
    {
        ASSERT_NE(catalog.entry(index), nullptr);
        EXPECT_EQ(catalog.entry(index)->identifier, source[index].identifier);
    }
    EXPECT_EQ(catalog.entry(catalog.entryCount()), nullptr);
}

TEST(SkyCatalogTest, ARegionAnswersWithWhatIsInsideItBrightestFirst)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, sampleEntries());

    an::CatalogRegionQuery query;
    query.center = an::SkyCoordinate{.rightAscensionDegrees = 314.7, .declinationDegrees = 44.35};
    query.radiusDegrees = 0.5;
    query.magnitudeLimit = 20.0;

    const an::Result<an::CatalogQueryResult> found = an::queryCatalogRegion(execution, catalog, query);
    ASSERT_TRUE(found.hasValue()) << found.status().message();
    EXPECT_EQ(identifiersOf(catalog, found->entryIndices),
              (std::vector<std::string>{"NGC 7000", "IC 5070", "NGC 6997"}));
    EXPECT_EQ(found->admittedCount, 3u);
    EXPECT_FALSE(found->truncated);

    // The declination index is what makes this cheaper than a scan: three of the seven entries lie
    // in the declination band the cone spans, and only those are measured against it.
    EXPECT_LT(found->examinedCount, catalog.entryCount());
}

TEST(SkyCatalogTest, ARegionIsExactAtItsEdge)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<an::CatalogEntry> entries;
    entries.push_back(makeEntry("CENTER", 100.0, 0.0, 1.0));
    entries.push_back(makeEntry("INSIDE", 100.9999, 0.0, 2.0));
    entries.push_back(makeEntry("OUTSIDE", 101.0001, 0.0, 3.0));
    const an::SkyCatalog catalog = buildOrFail(execution, std::move(entries));

    an::CatalogRegionQuery query;
    query.center = an::SkyCoordinate{.rightAscensionDegrees = 100.0, .declinationDegrees = 0.0};
    query.radiusDegrees = 1.0;

    const an::Result<an::CatalogQueryResult> found = an::queryCatalogRegion(execution, catalog, query);
    ASSERT_TRUE(found.hasValue()) << found.status().message();
    EXPECT_EQ(identifiersOf(catalog, found->entryIndices), (std::vector<std::string>{"CENTER", "INSIDE"}));
}

TEST(SkyCatalogTest, ARegionCrossesTheWrapAndThePole)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<an::CatalogEntry> entries;
    entries.push_back(makeEntry("BEFORE", 359.5, 0.0, 1.0));
    entries.push_back(makeEntry("AFTER", 0.5, 0.0, 2.0));
    entries.push_back(makeEntry("FAR", 180.0, 0.0, 3.0));
    entries.push_back(makeEntry("POLE", 12.0, 89.9, 4.0));
    entries.push_back(makeEntry("NEAR POLE", 200.0, 89.5, 5.0));
    const an::SkyCatalog catalog = buildOrFail(execution, std::move(entries));

    an::CatalogRegionQuery acrossWrap;
    acrossWrap.center = an::SkyCoordinate{.rightAscensionDegrees = 0.0, .declinationDegrees = 0.0};
    acrossWrap.radiusDegrees = 1.0;
    const an::Result<an::CatalogQueryResult> wrapped = an::queryCatalogRegion(execution, catalog, acrossWrap);
    ASSERT_TRUE(wrapped.hasValue()) << wrapped.status().message();
    EXPECT_EQ(identifiersOf(catalog, wrapped->entryIndices), (std::vector<std::string>{"BEFORE", "AFTER"}));

    // Two entries a hundred and eighty degrees apart in right ascension are half a degree and one
    // degree from the pole, so a one-degree cone on the pole holds both of them.
    an::CatalogRegionQuery aroundPole;
    aroundPole.center = an::SkyCoordinate{.rightAscensionDegrees = 0.0, .declinationDegrees = 90.0};
    aroundPole.radiusDegrees = 0.75;
    const an::Result<an::CatalogQueryResult> polar = an::queryCatalogRegion(execution, catalog, aroundPole);
    ASSERT_TRUE(polar.hasValue()) << polar.status().message();
    EXPECT_EQ(identifiersOf(catalog, polar->entryIndices), (std::vector<std::string>{"POLE", "NEAR POLE"}));
}

TEST(SkyCatalogTest, AMagnitudeLimitAdmitsANamedObjectNobodyHasMeasuredOnlyWhenAsked)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, sampleEntries());

    an::CatalogRegionQuery query;
    query.center = an::SkyCoordinate{.rightAscensionDegrees = 84.5, .declinationDegrees = -4.0};
    query.radiusDegrees = 2.0;
    query.magnitudeLimit = 6.0;

    const an::Result<an::CatalogQueryResult> strict = an::queryCatalogRegion(execution, catalog, query);
    ASSERT_TRUE(strict.hasValue()) << strict.status().message();
    EXPECT_EQ(identifiersOf(catalog, strict->entryIndices), (std::vector<std::string>{"M 42"}));

    query.admitNamedEntriesWithoutMagnitude = true;
    const an::Result<an::CatalogQueryResult> named = an::queryCatalogRegion(execution, catalog, query);
    ASSERT_TRUE(named.hasValue()) << named.status().message();

    // The named nebula with no photometry comes back; the anonymous entry beside it does not, and
    // an entry with no magnitude sorts after every entry that has one.
    EXPECT_EQ(identifiersOf(catalog, named->entryIndices), (std::vector<std::string>{"M 42", "Barnard 33"}));
}

TEST(SkyCatalogTest, EqualMagnitudesBreakTheirTieOnAreaThenIdentifier)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<an::CatalogEntry> entries;
    entries.push_back(makeEntry("NGC 3", 10.0, 10.0, 5.0));
    entries.push_back(makeEntry("NGC 1", 10.0, 10.0, 5.0));
    entries.push_back(makeEntry("NGC 2", 10.0, 10.0, 5.0));
    entries[2].extent = an::CatalogExtent{.majorAxisArcminutes = 10.0, .minorAxisArcminutes = 10.0};
    const an::SkyCatalog catalog = buildOrFail(execution, std::move(entries));

    an::CatalogRegionQuery query;
    query.center = an::SkyCoordinate{.rightAscensionDegrees = 10.0, .declinationDegrees = 10.0};
    query.radiusDegrees = 0.5;
    const an::Result<an::CatalogQueryResult> found = an::queryCatalogRegion(execution, catalog, query);
    ASSERT_TRUE(found.hasValue()) << found.status().message();
    EXPECT_EQ(identifiersOf(catalog, found->entryIndices), (std::vector<std::string>{"NGC 2", "NGC 1", "NGC 3"}));
}

TEST(SkyCatalogTest, ABoundedAnswerKeepsTheBrightestAndSaysItWasCut)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, sampleEntries());

    an::CatalogRegionQuery query;
    query.center = an::SkyCoordinate{.rightAscensionDegrees = 314.7, .declinationDegrees = 44.4};
    query.radiusDegrees = 1.0;
    query.maximumResults = 1u;

    const an::Result<an::CatalogQueryResult> found = an::queryCatalogRegion(execution, catalog, query);
    ASSERT_TRUE(found.hasValue()) << found.status().message();
    EXPECT_EQ(identifiersOf(catalog, found->entryIndices), (std::vector<std::string>{"NGC 7000"}));
    EXPECT_EQ(found->admittedCount, 3u);
    EXPECT_TRUE(found->truncated);
}

TEST(SkyCatalogTest, ARegionRefusesARequestThatIsNotOne)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, sampleEntries());

    an::CatalogRegionQuery query;
    query.center = an::SkyCoordinate{.rightAscensionDegrees = 10.0, .declinationDegrees = 10.0};

    query.radiusDegrees = 0.0;
    EXPECT_EQ(an::queryCatalogRegion(execution, catalog, query).status().code(), an::ErrorCode::InvalidArgument);
    query.radiusDegrees = std::numeric_limits<double>::infinity();
    EXPECT_EQ(an::queryCatalogRegion(execution, catalog, query).status().code(), an::ErrorCode::InvalidArgument);
    query.radiusDegrees = 1.0;
    query.maximumResults = 0u;
    EXPECT_EQ(an::queryCatalogRegion(execution, catalog, query).status().code(), an::ErrorCode::InvalidArgument);
    query.maximumResults = 10u;
    query.center.declinationDegrees = 95.0;
    EXPECT_EQ(an::queryCatalogRegion(execution, catalog, query).status().code(), an::ErrorCode::InvalidArgument);

    const an::SkyCatalog empty;
    an::CatalogRegionQuery valid;
    valid.center = an::SkyCoordinate{.rightAscensionDegrees = 10.0, .declinationDegrees = 10.0};
    EXPECT_EQ(an::queryCatalogRegion(execution, empty, valid).status().code(), an::ErrorCode::NotInitialized);
}

TEST(SkyCatalogTest, ANameIsLookedUpExactlyBeforeAnythingElse)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, sampleEntries());

    an::CatalogNameQuery query;
    query.text = "NGC 7000";
    const an::Result<an::CatalogQueryResult> exact = an::queryCatalogByName(execution, catalog, query);
    ASSERT_TRUE(exact.hasValue()) << exact.status().message();
    EXPECT_EQ(identifiersOf(catalog, exact->entryIndices), (std::vector<std::string>{"NGC 7000"}));

    // The common name is a key of the same index.
    query.text = "  Andromeda Galaxy ";
    const an::Result<an::CatalogQueryResult> byCommonName = an::queryCatalogByName(execution, catalog, query);
    ASSERT_TRUE(byCommonName.hasValue()) << byCommonName.status().message();
    EXPECT_EQ(identifiersOf(catalog, byCommonName->entryIndices), (std::vector<std::string>{"M 31"}));
}

TEST(SkyCatalogTest, ADesignationSomebodyTypedIsCanonicalizedBeforeTheSubstringScan)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, sampleEntries());

    an::CatalogNameQuery query;
    query.text = "ngc7000";
    const an::Result<an::CatalogQueryResult> normalized = an::queryCatalogByName(execution, catalog, query);
    ASSERT_TRUE(normalized.hasValue()) << normalized.status().message();
    EXPECT_EQ(identifiersOf(catalog, normalized->entryIndices), (std::vector<std::string>{"NGC 7000"}));

    query.normalizeDesignation = false;
    const an::Result<an::CatalogQueryResult> literal = an::queryCatalogByName(execution, catalog, query);
    ASSERT_TRUE(literal.hasValue()) << literal.status().message();
    EXPECT_TRUE(literal->entryIndices.empty());
}

TEST(SkyCatalogTest, ASubstringIsTheLastPassAndAnswersInCatalogOrder)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, sampleEntries());

    an::CatalogNameQuery query;
    query.text = "nebula";
    const an::Result<an::CatalogQueryResult> found = an::queryCatalogByName(execution, catalog, query);
    ASSERT_TRUE(found.hasValue()) << found.status().message();
    EXPECT_EQ(identifiersOf(catalog, found->entryIndices),
              (std::vector<std::string>{"NGC 7000", "IC 5070", "M 42", "Barnard 33"}));

    query.matchSubstring = false;
    const an::Result<an::CatalogQueryResult> exactOnly = an::queryCatalogByName(execution, catalog, query);
    ASSERT_TRUE(exactOnly.hasValue()) << exactOnly.status().message();
    EXPECT_TRUE(exactOnly->entryIndices.empty());
}

TEST(SkyCatalogTest, CaseFoldingIsAskedForRatherThanAssumed)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, sampleEntries());

    an::CatalogNameQuery query;
    query.text = "ngc 7000";
    const an::Result<an::CatalogQueryResult> folded = an::queryCatalogByName(execution, catalog, query);
    ASSERT_TRUE(folded.hasValue()) << folded.status().message();
    EXPECT_EQ(identifiersOf(catalog, folded->entryIndices), (std::vector<std::string>{"NGC 7000"}));

    query.caseInsensitive = false;
    query.normalizeDesignation = false;
    query.matchSubstring = false;
    const an::Result<an::CatalogQueryResult> exact = an::queryCatalogByName(execution, catalog, query);
    ASSERT_TRUE(exact.hasValue()) << exact.status().message();
    EXPECT_TRUE(exact->entryIndices.empty());
}

TEST(SkyCatalogTest, ACatalogWithoutANameIndexRefusesANameQuery)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::SkyCatalogRequest request;
    request.buildNameIndex = false;
    const an::SkyCatalog catalog = buildOrFail(execution, sampleEntries(), request);
    EXPECT_FALSE(catalog.hasNameIndex());

    an::CatalogNameQuery query;
    query.text = "NGC 7000";
    EXPECT_EQ(an::queryCatalogByName(execution, catalog, query).status().code(), an::ErrorCode::Unsupported);

    // The region query is unaffected: it is what such a catalog exists for.
    an::CatalogRegionQuery region;
    region.center = an::SkyCoordinate{.rightAscensionDegrees = 314.75, .declinationDegrees = 44.3167};
    region.radiusDegrees = 0.5;
    EXPECT_TRUE(an::queryCatalogRegion(execution, catalog, region).hasValue());
}

TEST(SkyCatalogTest, ANameQueryRefusesARequestThatIsNotOne)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, sampleEntries());

    an::CatalogNameQuery empty;
    EXPECT_EQ(an::queryCatalogByName(execution, catalog, empty).status().code(), an::ErrorCode::InvalidArgument);

    an::CatalogNameQuery spaces;
    spaces.text = "   ";
    EXPECT_EQ(an::queryCatalogByName(execution, catalog, spaces).status().code(), an::ErrorCode::InvalidArgument);

    an::CatalogNameQuery zero;
    zero.text = "M 31";
    zero.maximumResults = 0u;
    EXPECT_EQ(an::queryCatalogByName(execution, catalog, zero).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(SkyCatalogTest, TheIndexChangesTheCostAndNeverTheAnswer)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    // Forty cones over the whole sphere, at radii from one to nine degrees.
    std::vector<an::CatalogRegionQuery> cones;
    for (std::uint32_t index = 0u; index < 40u; ++index)
    {
        const double turn = static_cast<double>(index) / 40.0;
        an::CatalogRegionQuery query;
        query.center =
            an::SkyCoordinate{.rightAscensionDegrees = std::fmod(turn * 1440.0, 360.0),
                              .declinationDegrees = std::asin(2.0 * turn - 1.0 + 0.025) * 180.0 / std::numbers::pi};
        query.radiusDegrees = 1.0 + 8.0 * turn;
        cones.push_back(query);
    }

    // A field spread over the whole sphere by an area-preserving mapping, so a cone anywhere holds a
    // comparable number of entries and the declination band is never the whole table.
    std::vector<an::CatalogEntry> entries;
    for (std::uint32_t index = 0u; index < 2000u; ++index)
    {
        const double turn = static_cast<double>(index) * 0.6180339887498949;
        const double rightAscension = 360.0 * (turn - std::floor(turn));
        const double sine = 2.0 * (std::fmod(static_cast<double>(index) * 0.0173, 1.0)) - 1.0;
        entries.push_back(makeEntry("E " + std::to_string(index), rightAscension,
                                    std::asin(sine) * 180.0 / std::numbers::pi,
                                    5.0 + 0.001 * static_cast<double>(index)));
    }

    // Plus four entries per cone planted on its declination band, which is the one boundary the
    // index has no margin at: two a billionth of a radius inside the band and two the same distance
    // outside it. A band seek that is off by a hair drops or admits one of these, and the scan below
    // says which.
    for (std::uint32_t index = 0u; index < cones.size(); ++index)
    {
        const an::CatalogRegionQuery& cone = cones[index];
        const double insideRadius = cone.radiusDegrees * (1.0 - 1.0e-9);
        const double outsideRadius = cone.radiusDegrees * (1.0 + 1.0e-9);
        for (const double offset : {insideRadius, -insideRadius, outsideRadius, -outsideRadius})
        {
            const double declination = cone.center.declinationDegrees + offset;
            if (declination < -90.0 || declination > 90.0)
            {
                continue;
            }
            entries.push_back(makeEntry("BAND " + std::to_string(index) + " " + std::to_string(offset),
                                        cone.center.rightAscensionDegrees, declination,
                                        50.0 + 0.001 * static_cast<double>(entries.size())));
        }
    }
    const an::SkyCatalog catalog = buildOrFail(execution, std::move(entries));

    // Every cone is answered twice: once through the index, once by measuring every entry in the
    // catalog against the same separation. A band or window that ever excluded a real answer shows
    // up here as a missing entry rather than as a slightly faster query.
    std::uint32_t examinedThroughIndex = 0u;
    std::uint32_t examinedByScan = 0u;
    std::uint32_t planted = 0u;
    for (std::uint32_t index = 0u; index < cones.size(); ++index)
    {
        const an::CatalogRegionQuery& query = cones[index];
        const an::Result<an::CatalogQueryResult> found = an::queryCatalogRegion(execution, catalog, query);
        ASSERT_TRUE(found.hasValue()) << found.status().message();

        std::vector<std::uint32_t> expected;
        for (std::uint32_t entry = 0u; entry < catalog.entryCount(); ++entry)
        {
            if (an::angularSeparationDegrees(query.center, catalog.entry(entry)->coordinate) <= query.radiusDegrees)
            {
                expected.push_back(entry);
                planted += catalog.entry(entry)->identifier.rfind("BAND ", 0u) == 0u ? 1u : 0u;
            }
        }
        std::vector<std::uint32_t> answered = found->entryIndices;
        std::sort(answered.begin(), answered.end());
        EXPECT_EQ(answered, expected) << "cone " << index;
        EXPECT_EQ(found->admittedCount, expected.size());
        examinedThroughIndex += found->examinedCount;
        examinedByScan += catalog.entryCount();
    }

    // The planted entries are inside the cones they were planted for, so the comparison above is
    // actually comparing something at the band edge.
    EXPECT_GE(planted, cones.size());
    EXPECT_LT(examinedThroughIndex, examinedByScan / 2u);
}

TEST(SkyCatalogTest, AStoppedQueryReturnsCancellation)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, sampleEntries());

    std::stop_source stop;
    const an::ExecutionContext stopped = makeExecution(engine, 4u, stop.get_token());
    stop.request_stop();

    an::CatalogRegionQuery region;
    region.center = an::SkyCoordinate{.rightAscensionDegrees = 314.75, .declinationDegrees = 44.3167};
    region.radiusDegrees = 1.0;
    EXPECT_TRUE(an::isCancellation(an::queryCatalogRegion(stopped, catalog, region).status().code()));

    an::CatalogNameQuery name;
    name.text = "nebula";
    EXPECT_TRUE(an::isCancellation(an::queryCatalogByName(stopped, catalog, name).status().code()));

    EXPECT_TRUE(an::isCancellation(an::buildSkyCatalog(stopped, sampleEntries()).status().code()));
}

} // namespace

TEST(SkyCatalogTest, PersistentIndexesAreBudgetedAndHoldTheirContextAlive)
{
    std::vector<an::CatalogEntry> entries(1000u);
    for (std::size_t i = 0u; i < entries.size(); ++i)
    {
        entries[i].identifier = "catalog-with-a-long-designation-" + std::to_string(i);
        entries[i].commonName = "a separate long common name for object " + std::to_string(i);
    }
    an::EngineContextOptions options;
    options.maxConcurrency = 1u;
    for (const std::uint64_t budget : {1u, 4096u, 256u * 1024u, 512u * 1024u})
    {
        options.limits.memoryBudgetBytes = budget;
        auto engine = an::EngineContext::create(options);
        ASSERT_TRUE(engine.hasValue());
        auto execution = an::ExecutionContext::create(*engine, {});
        ASSERT_TRUE(execution.hasValue());
        const auto refused = an::buildSkyCatalog(*execution, entries);
        ASSERT_EQ(refused.code(), an::ErrorCode::MemoryBudgetExceeded);
        EXPECT_EQ(engine->memoryResource()->counters().liveBytes, 0u);
        EXPECT_EQ(engine->outstandingLeases(), 1u);
    }
    auto engine = makeEngine();
    an::SkyCatalog catalog;
    {
        const auto execution = makeExecution(engine);
        auto built = an::buildSkyCatalog(execution, entries);
        ASSERT_TRUE(built.hasValue()) << built.status().message();
        catalog = *built;
        EXPECT_GE(engine.memoryResource()->counters().liveBytes, entries.size() * sizeof(an::CatalogEntry));
        const auto named = an::queryCatalogByName(execution, catalog, {.text = entries[12].identifier});
        ASSERT_TRUE(named.hasValue());
        ASSERT_EQ(named->entryIndices.size(), 1u);
        EXPECT_EQ(named->entryIndices.front(), 12u);
    }
    EXPECT_EQ(engine.shutdown().code(), an::ErrorCode::Busy);
    EXPECT_EQ(catalog.entry(12u)->identifier, entries[12].identifier);
    auto copy = catalog;
    catalog = {};
    EXPECT_GT(engine.memoryResource()->counters().liveBytes, 0u);
    copy = {};
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    EXPECT_TRUE(engine.shutdown().ok());
}

// A catalog holds what its table and indices need. The entries and the declination index are each
// one table asked of the context one to one, so a catalog without a name index holds little more
// than they do; the name index pools its nodes and keys and adds a bounded amount an entry. A pool
// left to the library's defaults instead rounded a table up to a power of two inside a chunk of
// several such blocks, and a catalog held several times its tables.
TEST(SkyCatalogTest, ACatalogHoldsWhatItsTablesAndIndicesNeed)
{
    constexpr std::size_t kEntries = 60000u;
    std::vector<an::CatalogEntry> entries;
    entries.reserve(kEntries);
    for (std::size_t i = 0u; i < kEntries; ++i)
    {
        const double fraction = static_cast<double>(i) / static_cast<double>(kEntries);
        entries.push_back(makeEntry("S " + std::to_string(i), 360.0 * fraction,
                                    -89.0 + 178.0 * std::fmod(fraction * 7919.0, 1.0),
                                    2.0 + 7.0 * std::fmod(fraction * 104729.0, 1.0)));
    }
    // The tables alone: every entry, and its declination with its position.
    const std::uint64_t tables = kEntries * (sizeof(an::CatalogEntry) + sizeof(double) + sizeof(std::uint64_t));
    auto engine = makeEngine(8u, 1024ull * 1024ull * 1024ull);
    for (const bool nameIndex : {false, true})
    {
        const std::uint64_t before = engine.memoryResource()->counters().liveBytes;
        const auto execution = makeExecution(engine);
        an::SkyCatalogRequest request;
        request.buildNameIndex = nameIndex;
        const an::SkyCatalog catalog = buildOrFail(execution, entries, request);
        ASSERT_EQ(catalog.entryCount(), kEntries);
        const std::uint64_t held = engine.memoryResource()->counters().liveBytes - before;
        if (!nameIndex)
        {
            EXPECT_GE(held, tables);
            EXPECT_LE(held, tables + tables / 20u + 256u * 1024u)
                << "a catalog without a name index holds " << held << " bytes for " << tables << " bytes of tables";
        }
        else
        {
            EXPECT_LE(held, tables + kEntries * 640u)
                << "a catalog holds " << held << " bytes, " << held / kEntries << " an entry, with its name index";
        }
    }
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
}
