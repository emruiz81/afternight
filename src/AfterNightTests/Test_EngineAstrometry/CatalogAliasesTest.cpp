// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The rows one provider's table lists twice, and which of the two a query answers with.

#include <AfterNightEngine/Algorithms/Catalog/CatalogAliases.h>

#include "CatalogTestSupport.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
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

/// The shipping deep-sky table's shape: two survey rows, a Messier row, and the Caldwell rows that
/// name three of them again.
[[nodiscard]] std::vector<an::CatalogEntry> deepSkyEntries()
{
    std::vector<an::CatalogEntry> entries;
    entries.push_back(makeEntry("NGC 7000", 314.75, 44.31, 4.0, "North America Nebula"));
    entries.push_back(makeEntry("C 20", 314.76, 44.32, 4.0, "North America Nebula"));
    entries.push_back(makeEntry("IC 5070", 313.30, 44.36, 8.0, "Pelican Nebula"));
    entries.push_back(makeEntry("C 33", 311.00, 31.72, 7.0, "Eastern Veil Nebula"));
    entries.push_back(makeEntry("NGC 6992", 311.05, 31.70, 7.0, "Eastern Veil Nebula"));
    entries.push_back(makeEntry("M 31", 10.68, 41.27, 3.4, "Andromeda Galaxy"));
    entries.push_back(makeEntry("C 99", 190.00, -63.00, 9.0, "Coalsack Nebula"));
    return entries;
}

/// The alias relation of a catalog, failing the test rather than the caller.
[[nodiscard]] an::CatalogAliasIndex buildOrFailIndex(const an::ExecutionContext& execution,
                                                     const an::SkyCatalog& catalog,
                                                     const an::CatalogAliasRequest& request = {})
{
    an::Result<an::CatalogAliasIndex> built = an::buildCatalogAliasIndex(execution, catalog, request);
    EXPECT_TRUE(built.hasValue()) << built.status().message();
    return built.hasValue() ? std::move(*built) : an::CatalogAliasIndex{};
}

/// Every position, in catalog order, so a collapse can be applied to a whole table.
[[nodiscard]] std::vector<std::uint32_t> everyPosition(const an::SkyCatalog& catalog)
{
    std::vector<std::uint32_t> positions(catalog.entryCount());
    for (std::uint32_t index = 0u; index < catalog.entryCount(); ++index)
    {
        positions[index] = index;
    }
    return positions;
}

TEST(CatalogAliasesTest, AnAliasIsAnsweredForByTheRowThatNamesTheObject)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, deepSkyEntries());
    const an::CatalogAliasIndex index = buildOrFailIndex(execution, catalog);

    ASSERT_EQ(index.canonicalEntryIndices.size(), 7u);
    EXPECT_EQ(index.aliasCount, 2u);
    EXPECT_EQ(index.canonicalEntryIndices[1], 0u);
    EXPECT_EQ(index.canonicalEntryIndices[3], 4u);

    // Every other row answers for itself, including the alias whose object the table does not carry
    // under any other name.
    EXPECT_EQ(index.canonicalEntryIndices[0], 0u);
    EXPECT_EQ(index.canonicalEntryIndices[2], 2u);
    EXPECT_EQ(index.canonicalEntryIndices[4], 4u);
    EXPECT_EQ(index.canonicalEntryIndices[5], 5u);
    EXPECT_EQ(index.canonicalEntryIndices[6], 6u);
}

TEST(CatalogAliasesTest, TheRankingPrefersTheSurveyDesignationsInTheTablesOwnOrder)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    const auto answeredBy = [&](std::vector<an::CatalogEntry> entries) {
        const an::SkyCatalog catalog = buildOrFail(execution, std::move(entries));
        const an::CatalogAliasIndex index = buildOrFailIndex(execution, catalog);
        const an::CatalogEntry* const entry = catalog.entry(index.canonicalEntryIndices[0]);
        return entry == nullptr ? std::string{"<none>"} : entry->identifier;
    };

    // The alias is the nearest row to the last candidate every time, so only the rank decides.
    EXPECT_EQ(answeredBy({makeEntry("C 14", 20.0, 30.0, 5.0, "Double Cluster"),
                          makeEntry("PGC 1", 20.0, 30.0, 5.0, "Double Cluster"),
                          makeEntry("M 103", 20.0, 30.0, 5.0, "Double Cluster"),
                          makeEntry("IC 1", 20.0, 30.0, 5.0, "Double Cluster"),
                          makeEntry("NGC 869", 20.0, 30.0, 5.0, "Double Cluster")}),
              "NGC 869");
    EXPECT_EQ(answeredBy({makeEntry("C 14", 20.0, 30.0, 5.0, "Double Cluster"),
                          makeEntry("PGC 1", 20.0, 30.0, 5.0, "Double Cluster"),
                          makeEntry("M 103", 20.0, 30.0, 5.0, "Double Cluster"),
                          makeEntry("IC 1", 20.0, 30.0, 5.0, "Double Cluster")}),
              "IC 1");
    EXPECT_EQ(answeredBy({makeEntry("C 14", 20.0, 30.0, 5.0, "Double Cluster"),
                          makeEntry("PGC 1", 20.0, 30.0, 5.0, "Double Cluster"),
                          makeEntry("M 103", 20.0, 30.0, 5.0, "Double Cluster")}),
              "M 103");
    EXPECT_EQ(answeredBy({makeEntry("C 14", 20.0, 30.0, 5.0, "Double Cluster"),
                          makeEntry("PGC 1", 20.0, 30.0, 5.0, "Double Cluster")}),
              "PGC 1");
}

TEST(CatalogAliasesTest, EquallyGoodCandidatesAreChosenTheSameWayWhateverOrderTheyAreIn)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    std::vector<an::CatalogEntry> entries;
    entries.push_back(makeEntry("C 14", 20.0, 30.0, 5.0, "Double Cluster"));
    // The same direction, so the separations are the same bits and only the identifier can decide.
    entries.push_back(makeEntry("NGC 884", 20.0, 30.5, 5.0, "Double Cluster"));
    entries.push_back(makeEntry("NGC 869", 20.0, 30.5, 5.0, "Double Cluster"));

    const an::SkyCatalog forward = buildOrFail(execution, entries);
    const an::CatalogAliasIndex forwardIndex = buildOrFailIndex(execution, forward);
    ASSERT_NE(forward.entry(forwardIndex.canonicalEntryIndices[0]), nullptr);
    EXPECT_EQ(forward.entry(forwardIndex.canonicalEntryIndices[0])->identifier, "NGC 869");

    std::swap(entries[1], entries[2]);
    const an::SkyCatalog reversed = buildOrFail(execution, entries);
    const an::CatalogAliasIndex reversedIndex = buildOrFailIndex(execution, reversed);
    ASSERT_NE(reversed.entry(reversedIndex.canonicalEntryIndices[0]), nullptr);
    EXPECT_EQ(reversed.entry(reversedIndex.canonicalEntryIndices[0])->identifier, "NGC 869");
}

TEST(CatalogAliasesTest, ARowFurtherAwayThanTheToleranceIsADifferentObject)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<an::CatalogEntry> entries;
    entries.push_back(makeEntry("C 14", 20.0, 30.0, 5.0, "Double Cluster"));
    entries.push_back(makeEntry("NGC 869", 20.0, 34.0, 5.0, "Double Cluster"));
    const an::SkyCatalog catalog = buildOrFail(execution, entries);

    const an::CatalogAliasIndex outside = buildOrFailIndex(execution, catalog);
    EXPECT_EQ(outside.aliasCount, 0u);
    EXPECT_EQ(outside.canonicalEntryIndices[0], 0u);

    an::CatalogAliasRequest wider;
    wider.maximumSeparationDegrees = 5.0;
    const an::CatalogAliasIndex inside = buildOrFailIndex(execution, catalog, wider);
    EXPECT_EQ(inside.aliasCount, 1u);
    EXPECT_EQ(inside.canonicalEntryIndices[0], 1u);
}

TEST(CatalogAliasesTest, AnAliasNeedsANameAndACandidateThatIsNotItselfAnAlias)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    std::vector<an::CatalogEntry> nameless;
    nameless.push_back(makeEntry("C 14", 20.0, 30.0, 5.0));
    nameless.push_back(makeEntry("NGC 869", 20.0, 30.0, 5.0));
    const an::CatalogAliasIndex noName = buildOrFailIndex(execution, buildOrFail(execution, nameless));
    EXPECT_EQ(noName.aliasCount, 0u);

    std::vector<an::CatalogEntry> aliasesOnly;
    aliasesOnly.push_back(makeEntry("C 14", 20.0, 30.0, 5.0, "Double Cluster"));
    aliasesOnly.push_back(makeEntry("Caldwell 15", 20.0, 30.0, 5.0, "Double Cluster"));
    const an::CatalogAliasIndex noCandidate = buildOrFailIndex(execution, buildOrFail(execution, aliasesOnly));
    EXPECT_EQ(noCandidate.aliasCount, 0u);
}

TEST(CatalogAliasesTest, TheNameGroupingLooksAtFarFewerPairsThanTheTableHolds)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    std::vector<an::CatalogEntry> entries;
    for (std::uint32_t index = 0u; index < 400u; ++index)
    {
        entries.push_back(makeEntry("NGC " + std::to_string(1000u + index), static_cast<double>(index) * 0.5, 10.0,
                                    5.0 + static_cast<double>(index) * 0.001, "Object " + std::to_string(index)));
    }
    entries.push_back(makeEntry("C 1", 0.0, 10.0, 5.0, "Object 0"));
    entries.push_back(makeEntry("C 2", 0.5, 10.0, 5.001, "Object 1"));

    const an::SkyCatalog catalog = buildOrFail(execution, entries);
    const an::CatalogAliasIndex index = buildOrFailIndex(execution, catalog);
    EXPECT_EQ(index.aliasCount, 2u);
    EXPECT_EQ(index.canonicalEntryIndices[400], 0u);
    EXPECT_EQ(index.canonicalEntryIndices[401], 1u);

    // Comparing every alias against every other row would take two by four hundred and one.
    EXPECT_EQ(index.examinedPairCount, 2u);
    EXPECT_LT(index.examinedPairCount, 802u);
}

TEST(CatalogAliasesTest, BuildingRefusesWhatHasNoRelationToFind)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, deepSkyEntries());

    const an::ExecutionContext none;
    EXPECT_EQ(an::buildCatalogAliasIndex(none, catalog).status().code(), an::ErrorCode::NotInitialized);
    EXPECT_EQ(an::buildCatalogAliasIndex(execution, an::SkyCatalog{}).status().code(), an::ErrorCode::NotInitialized);

    an::CatalogAliasRequest negative;
    negative.maximumSeparationDegrees = -1.0;
    EXPECT_EQ(an::buildCatalogAliasIndex(execution, catalog, negative).status().code(), an::ErrorCode::InvalidArgument);

    an::CatalogAliasRequest unknown;
    unknown.maximumSeparationDegrees = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::buildCatalogAliasIndex(execution, catalog, unknown).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(CatalogAliasesTest, BuildingStopsWhenTheCallerAsksItTo)
{
    const an::EngineContext engine = makeEngine();
    std::stop_source stop;
    const an::ExecutionContext execution = makeExecution(engine, 4u, stop.get_token());
    const an::ExecutionContext building = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(building, deepSkyEntries());

    stop.request_stop();
    EXPECT_EQ(an::buildCatalogAliasIndex(execution, catalog).status().code(), an::ErrorCode::Cancelled);
}

TEST(CatalogAliasesTest, CollapsingAnswersWithOneRowPerObjectAtTheEarlierPosition)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, deepSkyEntries());
    const an::CatalogAliasIndex index = buildOrFailIndex(execution, catalog);

    an::Result<std::vector<std::uint32_t>> collapsed =
        an::collapseCatalogAliases(execution, index, everyPosition(catalog));
    ASSERT_TRUE(collapsed.hasValue()) << collapsed.status().message();
    EXPECT_EQ(identifiersOf(catalog, *collapsed),
              (std::vector<std::string>{"NGC 7000", "IC 5070", "NGC 6992", "M 31", "C 99"}));

    // The alias comes first in the table, and the row that answers for it takes its place rather
    // than appearing after it.
    const std::vector<std::uint32_t> aliasFirst{3u, 4u};
    an::Result<std::vector<std::uint32_t>> replaced = an::collapseCatalogAliases(execution, index, aliasFirst);
    ASSERT_TRUE(replaced.hasValue()) << replaced.status().message();
    EXPECT_EQ(identifiersOf(catalog, *replaced), (std::vector<std::string>{"NGC 6992"}));

    const std::vector<std::uint32_t> aliasAlone{3u};
    an::Result<std::vector<std::uint32_t>> alone = an::collapseCatalogAliases(execution, index, aliasAlone);
    ASSERT_TRUE(alone.hasValue()) << alone.status().message();
    EXPECT_EQ(identifiersOf(catalog, *alone), (std::vector<std::string>{"C 33"}));
}

TEST(CatalogAliasesTest, CollapsingRefusesAnAnswerFromAnotherCatalog)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, deepSkyEntries());
    const an::CatalogAliasIndex index = buildOrFailIndex(execution, catalog);

    const an::ExecutionContext none;
    EXPECT_EQ(an::collapseCatalogAliases(none, index, everyPosition(catalog)).status().code(),
              an::ErrorCode::NotInitialized);
    EXPECT_EQ(an::collapseCatalogAliases(execution, an::CatalogAliasIndex{}, everyPosition(catalog)).status().code(),
              an::ErrorCode::InvalidData);

    const std::vector<std::uint32_t> beyond{0u, 99u};
    const an::Status refused = an::collapseCatalogAliases(execution, index, beyond).status();
    EXPECT_EQ(refused.code(), an::ErrorCode::OutOfRange);

    an::Result<std::vector<std::uint32_t>> nothing =
        an::collapseCatalogAliases(execution, index, std::span<const std::uint32_t>{});
    ASSERT_TRUE(nothing.hasValue()) << nothing.status().message();
    EXPECT_TRUE(nothing->empty());
}

} // namespace
