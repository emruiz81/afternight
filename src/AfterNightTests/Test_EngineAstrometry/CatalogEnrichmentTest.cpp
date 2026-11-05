// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Attaching catalog identity to measured sources: the one-to-one rule, its bounds and its order.

#include <AfterNightEngine/Algorithms/Catalog/CatalogEnrichment.h>

#include "CatalogTestSupport.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace
{

using catalog_test::buildOrFail;
using catalog_test::makeEngine;
using catalog_test::makeEntry;
using catalog_test::makeExecution;

constexpr double kArcsecond = 1.0 / 3600.0;

[[nodiscard]] an::CatalogSourceMeasurement makeSource(double rightAscensionDegrees, double declinationDegrees,
                                                      std::optional<double> instrumentalMagnitude = {})
{
    an::CatalogSourceMeasurement source;
    source.coordinate =
        an::SkyCoordinate{.rightAscensionDegrees = rightAscensionDegrees, .declinationDegrees = declinationDegrees};
    source.instrumentalMagnitude = instrumentalMagnitude;
    source.hasCoordinate = true;
    return source;
}

/// Three reference stars a degree apart, so nothing matches by accident.
[[nodiscard]] std::vector<an::CatalogEntry> referenceEntries()
{
    std::vector<an::CatalogEntry> entries;
    entries.push_back(makeEntry("REF A", 100.0, 10.0, 11.5, "Alpha"));
    entries.push_back(makeEntry("REF B", 101.0, 10.0, 12.5, "Beta"));
    entries.push_back(makeEntry("REF C", 102.0, 10.0, 13.5, "Gamma"));
    return entries;
}

[[nodiscard]] std::vector<std::string> matchedIdentifiers(const an::SkyCatalog& catalog,
                                                          const std::vector<an::CatalogMatch>& matches)
{
    std::vector<std::string> names;
    names.reserve(matches.size());
    for (const an::CatalogMatch& match : matches)
    {
        const an::CatalogEntry* const entry = catalog.entry(match.entryIndex);
        names.push_back(entry == nullptr ? std::string{"<out-of-range>"} : entry->identifier);
    }
    return names;
}

TEST(CatalogEnrichmentTest, EachSourceTakesTheEntryItIsClosestTo)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, referenceEntries());

    const std::vector<an::CatalogSourceMeasurement> sources{
        makeSource(102.0 + kArcsecond, 10.0, -12.0),
        makeSource(100.0 - kArcsecond, 10.0, -14.0),
        makeSource(101.0, 10.0 + 2.0 * kArcsecond, -13.0),
    };

    const an::Result<an::CatalogMatchResult> matched = an::matchCatalogSources(execution, sources, catalog);
    ASSERT_TRUE(matched.hasValue()) << matched.status().message();
    EXPECT_EQ(matched->sourceCount, 3u);
    EXPECT_EQ(matched->positionedSourceCount, 3u);
    EXPECT_EQ(matched->matches.size(), 3u);

    // The answer is ordered by source position, whatever order the candidates were taken in.
    ASSERT_EQ(matched->matches.size(), 3u);
    EXPECT_EQ(matched->matches[0].sourceIndex, 0u);
    EXPECT_EQ(matched->matches[1].sourceIndex, 1u);
    EXPECT_EQ(matched->matches[2].sourceIndex, 2u);
    EXPECT_EQ(matchedIdentifiers(catalog, matched->matches), (std::vector<std::string>{"REF C", "REF A", "REF B"}));
    EXPECT_NEAR(matched->matches[0].separationArcseconds, 1.0 * std::cos(10.0 * 3.14159265358979323846 / 180.0),
                1.0e-6);
    EXPECT_NEAR(matched->matches[2].separationArcseconds, 2.0, 1.0e-9);

    ASSERT_TRUE(matched->faintestMatchedMagnitude.has_value());
    ASSERT_TRUE(matched->brightestMatchedMagnitude.has_value());
    EXPECT_DOUBLE_EQ(*matched->faintestMatchedMagnitude, 13.5);
    EXPECT_DOUBLE_EQ(*matched->brightestMatchedMagnitude, 11.5);
    EXPECT_GT(matched->workingBytes, 0u);
}

TEST(CatalogEnrichmentTest, TwoSourcesCannotShareOneEntry)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, referenceEntries());

    // Both sources are inside the radius of REF A. The nearer one takes it; the other takes nothing,
    // because the entry it would have matched is already spoken for.
    const std::vector<an::CatalogSourceMeasurement> sources{
        makeSource(100.0 + 3.0 * kArcsecond, 10.0),
        makeSource(100.0 + 1.0 * kArcsecond, 10.0),
    };

    const an::Result<an::CatalogMatchResult> matched = an::matchCatalogSources(execution, sources, catalog);
    ASSERT_TRUE(matched.hasValue()) << matched.status().message();
    EXPECT_EQ(matched->candidateCount, 2u);
    ASSERT_EQ(matched->matches.size(), 1u);
    EXPECT_EQ(matched->matches[0].sourceIndex, 1u);
    EXPECT_EQ(matched->matches[0].entryIndex, 0u);
}

TEST(CatalogEnrichmentTest, TheMatchRadiusIsWhatDecides)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, referenceEntries());
    const std::vector<an::CatalogSourceMeasurement> sources{makeSource(100.0, 10.0 + 4.0 * kArcsecond)};

    an::CatalogMatchRequest tight;
    tight.matchRadiusArcseconds = 3.0;
    const an::Result<an::CatalogMatchResult> refused = an::matchCatalogSources(execution, sources, catalog, tight);
    ASSERT_TRUE(refused.hasValue()) << refused.status().message();
    EXPECT_TRUE(refused->matches.empty());
    EXPECT_EQ(refused->candidateCount, 0u);
    EXPECT_FALSE(refused->faintestMatchedMagnitude.has_value());

    an::CatalogMatchRequest loose;
    loose.matchRadiusArcseconds = 5.0;
    const an::Result<an::CatalogMatchResult> accepted = an::matchCatalogSources(execution, sources, catalog, loose);
    ASSERT_TRUE(accepted.hasValue()) << accepted.status().message();
    ASSERT_EQ(accepted->matches.size(), 1u);
    EXPECT_NEAR(accepted->matches[0].separationArcseconds, 4.0, 1.0e-9);
}

TEST(CatalogEnrichmentTest, AMagnitudeLimitRemovesEntriesFromTheMatch)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, referenceEntries());
    const std::vector<an::CatalogSourceMeasurement> sources{makeSource(100.0, 10.0), makeSource(102.0, 10.0)};

    an::CatalogMatchRequest request;
    request.magnitudeLimit = 12.0;
    const an::Result<an::CatalogMatchResult> matched = an::matchCatalogSources(execution, sources, catalog, request);
    ASSERT_TRUE(matched.hasValue()) << matched.status().message();
    ASSERT_EQ(matched->matches.size(), 1u);
    EXPECT_EQ(matchedIdentifiers(catalog, matched->matches), (std::vector<std::string>{"REF A"}));
}

TEST(CatalogEnrichmentTest, ASourceWithNoWorldPositionIsCountedAndSkipped)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, referenceEntries());

    std::vector<an::CatalogSourceMeasurement> sources{makeSource(100.0, 10.0), makeSource(101.0, 10.0)};
    sources[0].hasCoordinate = false;
    sources[0].coordinate =
        an::SkyCoordinate{.rightAscensionDegrees = std::numeric_limits<double>::quiet_NaN(), .declinationDegrees = 0.0};

    const an::Result<an::CatalogMatchResult> matched = an::matchCatalogSources(execution, sources, catalog);
    ASSERT_TRUE(matched.hasValue()) << matched.status().message();
    EXPECT_EQ(matched->sourceCount, 2u);
    EXPECT_EQ(matched->positionedSourceCount, 1u);
    ASSERT_EQ(matched->matches.size(), 1u);
    EXPECT_EQ(matched->matches[0].sourceIndex, 1u);
}

TEST(CatalogEnrichmentTest, APositionedSourceThatIsNotADirectionIsWrongData)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, referenceEntries());

    std::vector<an::CatalogSourceMeasurement> sources{makeSource(100.0, 10.0)};
    sources[0].coordinate.declinationDegrees = 91.0;
    EXPECT_EQ(an::matchCatalogSources(execution, sources, catalog).status().code(), an::ErrorCode::InvalidData);
}

TEST(CatalogEnrichmentTest, TheRequestAndItsInputsAreChecked)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, referenceEntries());
    const std::vector<an::CatalogSourceMeasurement> sources{makeSource(100.0, 10.0)};

    an::CatalogMatchRequest request;
    request.matchRadiusArcseconds = 0.0;
    EXPECT_EQ(an::matchCatalogSources(execution, sources, catalog, request).status().code(),
              an::ErrorCode::InvalidArgument);
    request.matchRadiusArcseconds = std::numeric_limits<double>::infinity();
    EXPECT_EQ(an::matchCatalogSources(execution, sources, catalog, request).status().code(),
              an::ErrorCode::InvalidArgument);
    request.matchRadiusArcseconds = 5.0;
    request.magnitudeLimit = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::matchCatalogSources(execution, sources, catalog, request).status().code(),
              an::ErrorCode::InvalidArgument);
    request.magnitudeLimit.reset();
    request.maximumCandidates = 0u;
    EXPECT_EQ(an::matchCatalogSources(execution, sources, catalog, request).status().code(),
              an::ErrorCode::InvalidArgument);

    EXPECT_EQ(an::matchCatalogSources(execution, {}, catalog).status().code(), an::ErrorCode::EmptySelection);
    EXPECT_EQ(an::matchCatalogSources(execution, sources, an::SkyCatalog{}).status().code(),
              an::ErrorCode::NotInitialized);
    const an::ExecutionContext nothing;
    EXPECT_EQ(an::matchCatalogSources(nothing, sources, catalog).status().code(), an::ErrorCode::NotInitialized);
}

TEST(CatalogEnrichmentTest, TooManyCandidatesIsRefusedBeforeTheyAreHeld)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    // Twenty entries and twenty sources on top of each other: every pair is a candidate.
    std::vector<an::CatalogEntry> entries;
    std::vector<an::CatalogSourceMeasurement> sources;
    for (std::uint32_t index = 0u; index < 20u; ++index)
    {
        entries.push_back(makeEntry("REF " + std::to_string(index), 100.0, 10.0, 12.0 + 0.01 * index));
        sources.push_back(makeSource(100.0, 10.0));
    }
    const an::SkyCatalog catalog = buildOrFail(execution, std::move(entries));

    an::CatalogMatchRequest request;
    request.maximumCandidates = 100u;
    const an::Result<an::CatalogMatchResult> refused = an::matchCatalogSources(execution, sources, catalog, request);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::LimitExceeded);

    request.maximumCandidates = 400u;
    const an::Result<an::CatalogMatchResult> accepted = an::matchCatalogSources(execution, sources, catalog, request);
    ASSERT_TRUE(accepted.hasValue()) << accepted.status().message();
    EXPECT_EQ(accepted->candidateCount, 400u);
    EXPECT_EQ(accepted->matches.size(), 20u);
}

TEST(CatalogEnrichmentTest, ABudgetTooSmallForTheCandidatesIsRefused)
{
    const an::EngineContext engine = makeEngine(8u, 4096u);
    const an::ExecutionContext execution = makeExecution(engine);

    std::vector<an::CatalogEntry> entries;
    std::vector<an::CatalogSourceMeasurement> sources;
    for (std::uint32_t index = 0u; index < 40u; ++index)
    {
        entries.push_back(makeEntry("REF " + std::to_string(index), 100.0, 10.0, 12.0 + 0.01 * index));
        sources.push_back(makeSource(100.0, 10.0));
    }
    const auto catalogEngine = makeEngine();
    const auto catalogExecution = makeExecution(catalogEngine);
    const an::SkyCatalog catalog = buildOrFail(catalogExecution, std::move(entries));

    const an::Result<an::CatalogMatchResult> refused = an::matchCatalogSources(execution, sources, catalog);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
}

TEST(CatalogEnrichmentTest, AStoppedMatchReturnsCancellation)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, referenceEntries());
    const std::vector<an::CatalogSourceMeasurement> sources{makeSource(100.0, 10.0)};

    std::stop_source stop;
    const an::ExecutionContext stopped = makeExecution(engine, 4u, stop.get_token());
    stop.request_stop();
    EXPECT_TRUE(an::isCancellation(an::matchCatalogSources(stopped, sources, catalog).status().code()));
}

TEST(CatalogEnrichmentTest, TheAnswerDoesNotDependOnTheAdmittedWidth)
{
    const an::EngineContext engine = makeEngine();
    const an::SkyCatalog catalog = buildOrFail(makeExecution(engine), referenceEntries());
    std::vector<an::CatalogSourceMeasurement> sources;
    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        sources.push_back(makeSource(100.0 + static_cast<double>(index) + kArcsecond, 10.0));
    }

    const an::ExecutionContext narrow = makeExecution(engine, 1u);
    const an::ExecutionContext wide = makeExecution(engine, 8u);
    const an::Result<an::CatalogMatchResult> one = an::matchCatalogSources(narrow, sources, catalog);
    const an::Result<an::CatalogMatchResult> many = an::matchCatalogSources(wide, sources, catalog);
    ASSERT_TRUE(one.hasValue()) << one.status().message();
    ASSERT_TRUE(many.hasValue()) << many.status().message();
    EXPECT_EQ(one->matches, many->matches);
    EXPECT_EQ(one->candidateCount, many->candidateCount);
}

TEST(CatalogEnrichmentTest, CorrespondencesAreOrderedByWhicheverFieldIsAsked)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, referenceEntries());

    std::vector<an::CatalogMatch> matches{
        an::CatalogMatch{.sourceIndex = 0u, .entryIndex = 2u, .separationArcseconds = 3.0},
        an::CatalogMatch{.sourceIndex = 1u, .entryIndex = 0u, .separationArcseconds = 1.0},
        an::CatalogMatch{.sourceIndex = 2u, .entryIndex = 1u, .separationArcseconds = 2.0},
    };

    ASSERT_TRUE(an::sortCatalogMatches(matches, catalog, an::CatalogMatchSortKey::SeparationArcseconds).ok());
    EXPECT_EQ(matchedIdentifiers(catalog, matches), (std::vector<std::string>{"REF A", "REF B", "REF C"}));

    ASSERT_TRUE(an::sortCatalogMatches(matches, catalog, an::CatalogMatchSortKey::CatalogIdentifier,
                                       an::CatalogSortDirection::Descending)
                    .ok());
    EXPECT_EQ(matchedIdentifiers(catalog, matches), (std::vector<std::string>{"REF C", "REF B", "REF A"}));

    ASSERT_TRUE(an::sortCatalogMatches(matches, catalog, an::CatalogMatchSortKey::CatalogCommonName).ok());
    EXPECT_EQ(matchedIdentifiers(catalog, matches), (std::vector<std::string>{"REF A", "REF B", "REF C"}));

    ASSERT_TRUE(an::sortCatalogMatches(matches, catalog, an::CatalogMatchSortKey::CatalogMagnitude,
                                       an::CatalogSortDirection::Descending)
                    .ok());
    EXPECT_EQ(matchedIdentifiers(catalog, matches), (std::vector<std::string>{"REF C", "REF B", "REF A"}));

    ASSERT_TRUE(an::sortCatalogMatches(matches, catalog, an::CatalogMatchSortKey::SourceIndex).ok());
    EXPECT_EQ(matches[0].sourceIndex, 0u);
    EXPECT_EQ(matches[2].sourceIndex, 2u);

    ASSERT_TRUE(an::sortCatalogMatches(matches, catalog, an::CatalogMatchSortKey::EntryIndex).ok());
    EXPECT_EQ(matches[0].entryIndex, 0u);
    EXPECT_EQ(matches[2].entryIndex, 2u);
}

TEST(CatalogEnrichmentTest, AnEntryWithNoMagnitudeSortsAfterEveryMeasuredOneAscending)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<an::CatalogEntry> entries = referenceEntries();
    entries[1].magnitude.reset();
    const an::SkyCatalog catalog = buildOrFail(execution, std::move(entries));

    std::vector<an::CatalogMatch> matches{
        an::CatalogMatch{.sourceIndex = 0u, .entryIndex = 1u, .separationArcseconds = 1.0},
        an::CatalogMatch{.sourceIndex = 1u, .entryIndex = 0u, .separationArcseconds = 1.0},
        an::CatalogMatch{.sourceIndex = 2u, .entryIndex = 2u, .separationArcseconds = 1.0},
    };

    ASSERT_TRUE(an::sortCatalogMatches(matches, catalog, an::CatalogMatchSortKey::CatalogMagnitude).ok());
    EXPECT_EQ(matchedIdentifiers(catalog, matches), (std::vector<std::string>{"REF A", "REF C", "REF B"}));

    ASSERT_TRUE(an::sortCatalogMatches(matches, catalog, an::CatalogMatchSortKey::CatalogMagnitude,
                                       an::CatalogSortDirection::Descending)
                    .ok());
    EXPECT_EQ(matchedIdentifiers(catalog, matches), (std::vector<std::string>{"REF B", "REF C", "REF A"}));
}

TEST(CatalogEnrichmentTest, OrderingKeepsEqualCorrespondencesWhereTheyWere)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, referenceEntries());

    std::vector<an::CatalogMatch> matches{
        an::CatalogMatch{.sourceIndex = 7u, .entryIndex = 0u, .separationArcseconds = 1.0},
        an::CatalogMatch{.sourceIndex = 3u, .entryIndex = 0u, .separationArcseconds = 1.0},
        an::CatalogMatch{.sourceIndex = 5u, .entryIndex = 0u, .separationArcseconds = 1.0},
    };

    ASSERT_TRUE(an::sortCatalogMatches(matches, catalog, an::CatalogMatchSortKey::CatalogIdentifier).ok());
    EXPECT_EQ(matches[0].sourceIndex, 7u);
    EXPECT_EQ(matches[1].sourceIndex, 3u);
    EXPECT_EQ(matches[2].sourceIndex, 5u);
}

TEST(CatalogEnrichmentTest, OrderingRefusesWhatItCannotRead)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCatalog catalog = buildOrFail(execution, referenceEntries());

    std::vector<an::CatalogMatch> matches{
        an::CatalogMatch{.sourceIndex = 0u, .entryIndex = 0u, .separationArcseconds = 1.0},
        an::CatalogMatch{.sourceIndex = 1u, .entryIndex = 99u, .separationArcseconds = 2.0},
    };
    EXPECT_EQ(an::sortCatalogMatches(matches, catalog, an::CatalogMatchSortKey::CatalogIdentifier).code(),
              an::ErrorCode::OutOfRange);
    EXPECT_EQ(matches[0].entryIndex, 0u);
    EXPECT_EQ(matches[1].entryIndex, 99u);

    matches[1].entryIndex = 1u;
    EXPECT_EQ(an::sortCatalogMatches(matches, catalog, static_cast<an::CatalogMatchSortKey>(50)).code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::sortCatalogMatches(matches, catalog, an::CatalogMatchSortKey::SourceIndex,
                                     static_cast<an::CatalogSortDirection>(9))
                  .code(),
              an::ErrorCode::InvalidArgument);

    // An index-only key needs no catalog at all, so an empty one is not a refusal for it.
    EXPECT_TRUE(an::sortCatalogMatches(matches, an::SkyCatalog{}, an::CatalogMatchSortKey::SourceIndex).ok());
    EXPECT_EQ(an::sortCatalogMatches(matches, an::SkyCatalog{}, an::CatalogMatchSortKey::CatalogIdentifier).code(),
              an::ErrorCode::OutOfRange);
}

TEST(CatalogEnrichmentTest, SortKeyNamesAreStable)
{
    EXPECT_STREQ(an::catalogMatchSortKeyName(an::CatalogMatchSortKey::CatalogIdentifier), "CatalogIdentifier");
    EXPECT_STREQ(an::catalogMatchSortKeyName(an::CatalogMatchSortKey::CatalogMagnitude), "CatalogMagnitude");
    EXPECT_STREQ(an::catalogMatchSortKeyName(static_cast<an::CatalogMatchSortKey>(50)), "UnknownCatalogMatchSortKey");
    EXPECT_TRUE(an::isCatalogMatchSortKey(an::CatalogMatchSortKey::SourceIndex));
    EXPECT_FALSE(an::isCatalogMatchSortKey(static_cast<an::CatalogMatchSortKey>(50)));
}

} // namespace
