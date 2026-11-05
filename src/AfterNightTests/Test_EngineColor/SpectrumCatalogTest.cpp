// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for the sampled-spectrum catalog: manifest, chunk container, owned table, lookups,
// chunk selection, readiness and the recorded-match join.

#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include "ColorTestSupport.h"
#include "SpectrumCatalogTestSupport.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace
{

using color_test::makeEngine;
using color_test::makeExecution;
using spectrum_test::ChunkOptions;
using spectrum_test::FixtureChunk;
using spectrum_test::FixtureSpectrumStar;
using spectrum_test::identifierAt;
using spectrum_test::makeChunkBytes;
using spectrum_test::makeManifestText;
using spectrum_test::ManifestOptions;
using spectrum_test::parseOrFail;
using spectrum_test::starAt;
using spectrum_test::tileSixOf;

[[nodiscard]] std::vector<FixtureChunk> undividedChunks(std::vector<FixtureSpectrumStar> stars)
{
    FixtureChunk chunk;
    chunk.stars = std::move(stars);
    return {chunk};
}

[[nodiscard]] an::ErrorCode manifestCode(const an::ExecutionContext& execution, const std::vector<FixtureChunk>& chunks,
                                         const ManifestOptions& options)
{
    const an::Result<an::SpectrumCatalogManifest> parsed =
        an::parseSpectrumCatalogManifest(execution, makeManifestText(chunks, options));
    return parsed.hasValue() ? an::ErrorCode::Ok : parsed.status().code();
}

TEST(SpectrumCatalogTest, PersistentTablesAreBudgetedAndKeepTheirContextAlive)
{
    an::SpectrumCatalogManifest manifest;
    manifest.chunks.resize(1u);
    an::SpectrumChunk chunk;
    chunk.records.resize(1000u);
    for (std::size_t index = 0u; index < chunk.records.size(); ++index)
    {
        chunk.records[index].sourceIdentifier = index + 1u;
        chunk.records[index].coordinate = {80., 20.};
    }
    chunk.flux.resize(chunk.records.size() * manifest.grid.sampleCount, 1.f);
    chunk.fluxError.resize(chunk.flux.size(), .1f);
    an::EngineContextOptions options;
    options.maxConcurrency = 1u;
    for (const std::uint64_t budget : {1u, 4096u, 1024u * 1024u, 2u * 1024u * 1024u})
    {
        options.limits.memoryBudgetBytes = budget;
        auto tiny = an::EngineContext::create(options);
        ASSERT_TRUE(tiny.hasValue());
        const auto tinyExecution = makeExecution(*tiny, 1u);
        const auto refused = an::buildSpectrumCatalog(tinyExecution, {chunk}, manifest);
        EXPECT_EQ(refused.code(), an::ErrorCode::MemoryBudgetExceeded);
        EXPECT_EQ(tinyExecution.memoryResource()->counters().liveBytes, 0u);
        EXPECT_EQ(tiny->outstandingLeases(), 1u);
    }

    auto engine = makeEngine();
    an::SpectrumCatalog catalog;
    {
        const auto execution = makeExecution(engine);
        auto built = an::buildSpectrumCatalog(execution, {chunk}, manifest);
        ASSERT_TRUE(built.hasValue()) << built.status().message();
        catalog = *built;
        EXPECT_GE(execution.memoryResource()->counters().liveBytes, chunk.flux.size() * sizeof(float) * 2u);
    }
    ASSERT_EQ(engine.shutdown().code(), an::ErrorCode::Busy);
    EXPECT_EQ(catalog.flux(0u).front(), 1.f);
    catalog = {};
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
    EXPECT_TRUE(engine.shutdown().ok());
}

TEST(SpectrumCatalogTest, ManifestParsesEveryStatedFactAndRefusesEachDeviation)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<FixtureChunk> chunks(2u);
    chunks[0].file = "chunks/a.anxp";
    chunks[0].firstTile = 100;
    chunks[0].tileCount = 64;
    chunks[0].stars = {starAt(10.0, 5.0, 1u), starAt(10.1, 5.1, 2u)};
    chunks[0].sizeBytes = 4096u;
    chunks[1].file = "chunks/b.anxp";
    chunks[1].firstTile = 164;
    chunks[1].tileCount = 64;
    chunks[1].stars = {starAt(20.0, -5.0, 3u)};
    ManifestOptions options;
    options.accessProfile = "installed-regional-cache";
    options.chunkManifestFile = "chunk_manifest.json";

    const an::SpectrumCatalogManifest manifest = parseOrFail(execution, chunks, options);
    EXPECT_EQ(manifest.schemaVersion, an::kXpCatalogManifestSchema);
    EXPECT_EQ(manifest.catalogFamilyId, an::kXpCatalogFamilyId);
    EXPECT_EQ(manifest.manifestId, "manifest-test-001");
    EXPECT_EQ(manifest.accessProfile, an::SpectrumCatalogAccessProfile::InstalledRegionalCache);
    EXPECT_EQ(manifest.sourceKind, an::SpectrumSourceKind::GaiaDr3XpSampled);
    EXPECT_EQ(manifest.totalRecords, 3u);
    EXPECT_EQ(manifest.recordSizeBytes, an::kXpChunkRecordBytes);
    EXPECT_TRUE(an::isXpSampledGrid(manifest.grid));
    EXPECT_EQ(manifest.rightsReviewStatus, "approved-for-bundling");
    EXPECT_EQ(manifest.chunkManifestFile, "chunk_manifest.json");
    EXPECT_EQ(manifest.coverageSummaryFile, "coverage_summary.json");
    EXPECT_TRUE(manifest.sourceIdentifierIndexFile.empty());
    ASSERT_EQ(manifest.chunks.size(), 2u);
    EXPECT_EQ(manifest.chunks[0].file, "chunks/a.anxp");
    EXPECT_EQ(manifest.chunks[0].firstTile, 100);
    EXPECT_EQ(manifest.chunks[0].tileCount, 64);
    EXPECT_EQ(manifest.chunks[0].recordCount, 2u);
    EXPECT_EQ(manifest.chunks[0].sizeBytes, 4096u);
    EXPECT_EQ(manifest.chunks[1].chunkIndex, 1);
    EXPECT_NEAR(an::spectralGridWavelengthNm(manifest.grid, 342u), 1020.0, 1.0e-12);

    ManifestOptions bad = options;
    bad.schema = "other";
    EXPECT_EQ(manifestCode(execution, chunks, bad), an::ErrorCode::InvalidData);
    bad = options;
    bad.family = "spcc_other";
    EXPECT_EQ(manifestCode(execution, chunks, bad), an::ErrorCode::InvalidData);
    bad = options;
    bad.manifestId = std::string(an::kXpCatalogFamilyId);
    EXPECT_EQ(manifestCode(execution, chunks, bad), an::ErrorCode::InvalidData);
    bad = options;
    bad.accessProfile = "carrier-pigeon";
    EXPECT_EQ(manifestCode(execution, chunks, bad), an::ErrorCode::Unsupported);
    bad = options;
    bad.sourceKind = "unknown";
    EXPECT_EQ(manifestCode(execution, chunks, bad), an::ErrorCode::Unsupported);
    bad = options;
    bad.magic = "ANSP";
    EXPECT_EQ(manifestCode(execution, chunks, bad), an::ErrorCode::InvalidData);
    bad = options;
    bad.byteOrder = "big-endian";
    EXPECT_EQ(manifestCode(execution, chunks, bad), an::ErrorCode::InvalidData);
    bad = options;
    bad.recordSizeBytes = 1024u;
    EXPECT_EQ(manifestCode(execution, chunks, bad), an::ErrorCode::InvalidData);
    bad = options;
    bad.healpixOrder = 5;
    EXPECT_EQ(manifestCode(execution, chunks, bad), an::ErrorCode::InvalidData);
    bad = options;
    bad.stepNm = 1.0;
    EXPECT_EQ(manifestCode(execution, chunks, bad), an::ErrorCode::InvalidData);
    bad = options;
    bad.statedTotalRecords = 7u;
    EXPECT_EQ(manifestCode(execution, chunks, bad), an::ErrorCode::InvalidData);

    std::vector<FixtureChunk> overlapping = chunks;
    overlapping[1].firstTile = 150;
    EXPECT_EQ(manifestCode(execution, overlapping, options), an::ErrorCode::InvalidData);
    std::vector<FixtureChunk> escaping = chunks;
    escaping[1].file = "../b.anxp";
    EXPECT_EQ(manifestCode(execution, escaping, options), an::ErrorCode::InvalidData);
    std::vector<FixtureChunk> absolute = chunks;
    absolute[1].file = "/b.anxp";
    EXPECT_EQ(manifestCode(execution, absolute, options), an::ErrorCode::InvalidData);
    std::vector<FixtureChunk> repeated = chunks;
    repeated[1].file = chunks[0].file;
    EXPECT_EQ(manifestCode(execution, repeated, options), an::ErrorCode::InvalidData);
    std::vector<FixtureChunk> outside = chunks;
    outside[1].firstTile = 49150;
    EXPECT_EQ(manifestCode(execution, outside, options), an::ErrorCode::InvalidData);
    EXPECT_EQ(manifestCode(execution, {}, options), an::ErrorCode::InvalidData);

    const an::Result<an::SpectrumCatalogManifest> notObject = an::parseSpectrumCatalogManifest(execution, "[1,2]");
    ASSERT_FALSE(notObject.hasValue());
    EXPECT_EQ(notObject.status().code(), an::ErrorCode::InvalidData);
    const an::Result<an::SpectrumCatalogManifest> tooLong =
        an::parseSpectrumCatalogManifest(execution, makeManifestText(chunks, options), {.maximumBytes = 16u});
    ASSERT_FALSE(tooLong.hasValue());
    EXPECT_EQ(tooLong.status().code(), an::ErrorCode::LimitExceeded);
    const an::Result<an::SpectrumCatalogManifest> uninitialized =
        an::parseSpectrumCatalogManifest(an::ExecutionContext{}, makeManifestText(chunks, options));
    ASSERT_FALSE(uninitialized.hasValue());
    EXPECT_EQ(uninitialized.status().code(), an::ErrorCode::NotInitialized);

    EXPECT_STREQ(an::spectrumSourceKindName(an::SpectrumSourceKind::SyntheticFixture), "SyntheticFixture");
    EXPECT_STREQ(an::spectrumCatalogAccessProfileName(an::SpectrumCatalogAccessProfile::Blocked), "blocked");
    EXPECT_FALSE(an::isSpectrumSourceKind(static_cast<an::SpectrumSourceKind>(3u)));
    EXPECT_FALSE(an::isSpectrumCatalogAccessProfile(static_cast<an::SpectrumCatalogAccessProfile>(4u)));
}

TEST(SpectrumCatalogTest, ChunkDecodesRecordsAndSpectraAndRefusesEachMalformedContainer)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    FixtureSpectrumStar first = starAt(30.0, 10.0, 1u, 11.5f);
    first.properMotionRightAscension = 12.5f;
    first.properMotionDeclination = -3.0f;
    first.qualityFlags = 0u;
    FixtureSpectrumStar second = starAt(30.2, 10.1, 2u, 13.0f);
    second.magnitudeBp = std::numeric_limits<float>::quiet_NaN();
    second.qualityFlags = 5u;
    second.fluxScale = 2.0e-18f;
    const std::vector<FixtureChunk> chunks = undividedChunks({first, second});
    const an::SpectrumCatalogManifest manifest = parseOrFail(execution, chunks);

    const std::vector<std::byte> bytes = makeChunkBytes(chunks[0], 0u);
    const an::Result<an::SpectrumChunk> decoded = an::decodeSpectrumChunk(execution, bytes, manifest, 0u);
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
    EXPECT_EQ(decoded->chunkIndex, 0);
    EXPECT_TRUE(an::isXpSampledGrid(decoded->grid));
    ASSERT_EQ(decoded->records.size(), 2u);
    EXPECT_EQ(decoded->flux.size(), 2u * an::kXpSampledWavelengthCount);
    EXPECT_EQ(decoded->fluxError.size(), 2u * an::kXpSampledWavelengthCount);
    const an::SpectrumRecord& recordA = decoded->records[0];
    EXPECT_EQ(recordA.sourceIdentifier, first.sourceIdentifier);
    EXPECT_NEAR(recordA.coordinate.rightAscensionDegrees, 30.0, 1.0e-12);
    EXPECT_NEAR(recordA.coordinate.declinationDegrees, 10.0, 1.0e-12);
    ASSERT_TRUE(recordA.properMotionRightAscensionMasPerYear.has_value());
    EXPECT_NEAR(*recordA.properMotionRightAscensionMasPerYear, 12.5, 1.0e-6);
    ASSERT_TRUE(recordA.properMotionDeclinationMasPerYear.has_value());
    EXPECT_NEAR(*recordA.properMotionDeclinationMasPerYear, -3.0, 1.0e-6);
    EXPECT_NEAR(recordA.referenceEpochYear, 2016.0, 1.0e-9);
    ASSERT_TRUE(recordA.magnitudeG.has_value());
    EXPECT_NEAR(*recordA.magnitudeG, 11.5, 1.0e-6);
    EXPECT_EQ(recordA.qualityFlags, 0u);
    EXPECT_EQ(recordA.recordIndex, 0u);
    const an::SpectrumRecord& recordB = decoded->records[1];
    EXPECT_FALSE(recordB.properMotionRightAscensionMasPerYear.has_value());
    EXPECT_FALSE(recordB.magnitudeBp.has_value());
    ASSERT_TRUE(recordB.magnitudeRp.has_value());
    EXPECT_EQ(recordB.qualityFlags, 5u);
    EXPECT_EQ(recordB.recordIndex, 1u);
    for (std::uint32_t index = 0u; index < an::kXpSampledWavelengthCount; index += 41u)
    {
        EXPECT_FLOAT_EQ(decoded->flux[index], static_cast<float>(first.quantizedFlux(index)) * first.fluxScale);
        EXPECT_FLOAT_EQ(decoded->fluxError[index],
                        static_cast<float>(first.quantizedError(index)) * first.fluxErrorScale);
        EXPECT_FLOAT_EQ(decoded->flux[an::kXpSampledWavelengthCount + index],
                        static_cast<float>(second.quantizedFlux(index)) * second.fluxScale);
    }

    const auto codeOf = [&](const std::vector<std::byte>& container, const std::uint32_t chunkIndex = 0u,
                            const an::SpectrumChunkLimits& limits = {}) {
        const an::Result<an::SpectrumChunk> result =
            an::decodeSpectrumChunk(execution, container, manifest, chunkIndex, limits);
        return result.hasValue() ? an::ErrorCode::Ok : result.status().code();
    };
    EXPECT_EQ(codeOf(bytes, 1u), an::ErrorCode::OutOfRange);
    EXPECT_EQ(codeOf(bytes, 0u, {.maximumRecords = 1u}), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(codeOf(bytes, 0u, {.maximumHeaderBytes = 8u}), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(codeOf(makeChunkBytes(chunks[0], 0u, {}, {.magic = "ANSP"})), an::ErrorCode::InvalidData);
    EXPECT_EQ(codeOf(makeChunkBytes(chunks[0], 0u, {}, {.formatVersion = 2u})), an::ErrorCode::InvalidData);
    EXPECT_EQ(codeOf(makeChunkBytes(chunks[0], 0u, {}, {.statedRecordCount = 3u})), an::ErrorCode::InvalidData);
    EXPECT_EQ(codeOf(makeChunkBytes(chunks[0], 0u, {}, {.headerFamily = "other"})), an::ErrorCode::InvalidData);
    EXPECT_EQ(codeOf(makeChunkBytes(chunks[0], 0u, {}, {.headerRecordCount = 9u})), an::ErrorCode::InvalidData);
    EXPECT_EQ(codeOf(makeChunkBytes(chunks[0], 0u, {}, {.headerOverride = "[]"})), an::ErrorCode::InvalidData);
    EXPECT_EQ(codeOf(makeChunkBytes(chunks[0], 0u, {}, {.extraTrailingBytes = 4u})), an::ErrorCode::InvalidData);
    std::vector<std::byte> truncated = bytes;
    truncated.resize(8u);
    EXPECT_EQ(codeOf(truncated), an::ErrorCode::InvalidData);

    const auto withStar = [&](const FixtureSpectrumStar& star) {
        FixtureChunk chunk = chunks[0];
        chunk.stars = {star};
        const an::SpectrumCatalogManifest single = parseOrFail(execution, {chunk});
        const an::Result<an::SpectrumChunk> result =
            an::decodeSpectrumChunk(execution, makeChunkBytes(chunk, 0u), single, 0u);
        return result.hasValue() ? an::ErrorCode::Ok : result.status().code();
    };
    FixtureSpectrumStar zeroId = first;
    zeroId.sourceIdentifier = 0u;
    EXPECT_EQ(withStar(zeroId), an::ErrorCode::InvalidData);
    FixtureSpectrumStar offSky = first;
    offSky.declinationDegrees = 95.0;
    EXPECT_EQ(withStar(offSky), an::ErrorCode::InvalidData);
    FixtureSpectrumStar wrongSamples = first;
    wrongSamples.validSampleCount = 300u;
    EXPECT_EQ(withStar(wrongSamples), an::ErrorCode::InvalidData);
    FixtureSpectrumStar badScale = first;
    badScale.fluxScale = 0.0f;
    EXPECT_EQ(withStar(badScale), an::ErrorCode::InvalidData);
    FixtureSpectrumStar wrongKind = first;
    wrongKind.sourceKind = 2u;
    EXPECT_EQ(withStar(wrongKind), an::ErrorCode::InvalidData);
    EXPECT_EQ(withStar(first), an::ErrorCode::Ok);

    std::stop_source stop;
    stop.request_stop();
    const an::Result<an::SpectrumChunk> cancelled =
        an::decodeSpectrumChunk(makeExecution(engine, 2u, stop.get_token()), bytes, manifest, 0u);
    ASSERT_FALSE(cancelled.hasValue());
    EXPECT_EQ(cancelled.status().code(), an::ErrorCode::Cancelled);
}

TEST(SpectrumCatalogTest, BuiltCatalogAnswersIdentifierAndRegionLookupsBrightestFirst)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<FixtureChunk> chunks(2u);
    chunks[0].file = "a.anxp";
    chunks[0].stars = {starAt(180.0, 0.0, 1u, 9.5f), starAt(180.01, 0.0, 2u, 13.0f), starAt(181.0, 0.0, 3u, 11.0f)};
    chunks[1].file = "b.anxp";
    chunks[1].stars = {starAt(180.02, 0.01, 4u, 9.5f)};
    chunks[1].stars[0].magnitudeG = std::numeric_limits<float>::quiet_NaN();
    const std::uint32_t tileA = tileSixOf(180.0, 0.0);
    const std::uint32_t tileB = tileSixOf(181.0, 0.0);
    chunks[0].firstTile = static_cast<std::int32_t>((std::min)(tileA, tileB));
    chunks[0].tileCount = static_cast<std::int32_t>((std::max)(tileA, tileB) - (std::min)(tileA, tileB) + 1u);
    chunks[1].firstTile = chunks[0].firstTile + chunks[0].tileCount;
    chunks[1].tileCount = 1;
    const an::SpectrumCatalog catalog = spectrum_test::buildOrFail(execution, chunks);
    ASSERT_TRUE(catalog.valid());
    EXPECT_EQ(catalog.recordCount(), 4u);
    ASSERT_EQ(catalog.loadedChunkIndices().size(), 2u);
    EXPECT_EQ(catalog.loadedChunkIndices()[0], 0);
    EXPECT_EQ(catalog.loadedChunkIndices()[1], 1);
    EXPECT_EQ(catalog.manifest().manifestId, "manifest-test-001");
    EXPECT_EQ(catalog.flux(0u).size(), an::kXpSampledWavelengthCount);
    EXPECT_EQ(catalog.fluxError(3u).size(), an::kXpSampledWavelengthCount);
    EXPECT_TRUE(catalog.flux(4u).empty());
    EXPECT_EQ(catalog.record(4u), nullptr);

    const std::optional<std::uint32_t> found = catalog.findSourceIdentifier(chunks[1].stars[0].sourceIdentifier);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, 3u);
    EXPECT_EQ(catalog.record(*found)->chunkIndex, 1);
    EXPECT_FALSE(catalog.findSourceIdentifier(12345u).has_value());
    EXPECT_FALSE(catalog.findSourceIdentifier(0u).has_value());
    EXPECT_FLOAT_EQ(catalog.flux(*found)[10],
                    static_cast<float>(chunks[1].stars[0].quantizedFlux(10u)) * chunks[1].stars[0].fluxScale);

    an::CatalogRegionQuery query;
    query.center = {.rightAscensionDegrees = 180.0, .declinationDegrees = 0.0};
    query.radiusDegrees = 0.05;
    query.magnitudeLimit = 12.0;
    const an::Result<an::CatalogQueryResult> limited = an::querySpectrumCatalogRegion(execution, catalog, query);
    ASSERT_TRUE(limited.hasValue()) << limited.status().message();
    ASSERT_EQ(limited->entryIndices.size(), 1u);
    EXPECT_EQ(catalog.record(limited->entryIndices[0])->sourceIdentifier, chunks[0].stars[0].sourceIdentifier);
    EXPECT_EQ(limited->admittedCount, 1u);
    EXPECT_GE(limited->examinedCount, 3u);

    query.magnitudeLimit.reset();
    const an::Result<an::CatalogQueryResult> all = an::querySpectrumCatalogRegion(execution, catalog, query);
    ASSERT_TRUE(all.hasValue()) << all.status().message();
    ASSERT_EQ(all->entryIndices.size(), 3u);
    EXPECT_EQ(catalog.record(all->entryIndices[0])->sourceIdentifier, chunks[0].stars[0].sourceIdentifier);
    EXPECT_EQ(catalog.record(all->entryIndices[1])->sourceIdentifier, chunks[0].stars[1].sourceIdentifier);
    EXPECT_EQ(catalog.record(all->entryIndices[2])->sourceIdentifier, chunks[1].stars[0].sourceIdentifier);

    query.magnitudeLimit = 12.0;
    query.admitNamedEntriesWithoutMagnitude = true;
    const an::Result<an::CatalogQueryResult> admitted = an::querySpectrumCatalogRegion(execution, catalog, query);
    ASSERT_TRUE(admitted.hasValue());
    EXPECT_EQ(admitted->entryIndices.size(), 2u);

    query.maximumResults = 1u;
    const an::Result<an::CatalogQueryResult> truncated = an::querySpectrumCatalogRegion(execution, catalog, query);
    ASSERT_TRUE(truncated.hasValue());
    EXPECT_TRUE(truncated->truncated);
    EXPECT_EQ(truncated->entryIndices.size(), 1u);
    EXPECT_EQ(truncated->admittedCount, 2u);

    query.radiusDegrees = 0.0;
    EXPECT_EQ(an::querySpectrumCatalogRegion(execution, catalog, query).status().code(),
              an::ErrorCode::InvalidArgument);
    query.radiusDegrees = 0.05;
    query.center.declinationDegrees = 91.0;
    EXPECT_EQ(an::querySpectrumCatalogRegion(execution, catalog, query).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::querySpectrumCatalogRegion(execution, an::SpectrumCatalog{}, query).status().code(),
              an::ErrorCode::NotInitialized);

    const an::SpectrumCatalog copy = catalog;
    EXPECT_EQ(copy.recordCount(), 4u);
    const an::SpectrumCatalog empty{};
    EXPECT_FALSE(empty.valid());
    EXPECT_EQ(empty.recordCount(), 0u);
    EXPECT_TRUE(an::isXpSampledGrid(empty.grid()));
}

// A region's records are ordered by a total order: exact magnitude, then identifier. Magnitudes one
// float step apart are closer than a micromagnitude, so an order that treated them as equal and fell
// to the identifier would call the first two stars equal, the second two equal and the first two
// different - an intransitive comparison, which a standard sort may not be handed at all. The
// identifiers run opposite to the brightness here, which is what makes such an order disagree with
// the exact one, and the fourth star ties the second exactly so the identifier still decides a true
// tie.
TEST(SpectrumCatalogTest, RegionOrderIsTotalWhenMagnitudesDifferByLessThanAMicromagnitude)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const float brightest = 10.0f;
    const float middle = std::nextafter(brightest, 11.0f);
    const float faintest = std::nextafter(middle, 11.0f);
    ASSERT_LT(static_cast<double>(middle) - static_cast<double>(brightest), 1.0e-6);
    ASSERT_GT(static_cast<double>(faintest) - static_cast<double>(brightest), 1.0e-6);
    const std::vector<FixtureChunk> chunks =
        undividedChunks({starAt(180.0, 0.0002, 3u, brightest), starAt(180.0, 0.0001, 2u, middle),
                         starAt(180.0, 0.0, 1u, faintest), starAt(180.0, 0.00015, 0u, middle)});
    const an::SpectrumCatalog catalog = spectrum_test::buildOrFail(execution, chunks);
    ASSERT_TRUE(catalog.valid());

    an::CatalogRegionQuery query;
    query.center = {.rightAscensionDegrees = 180.0, .declinationDegrees = 0.0001};
    query.radiusDegrees = 0.01;
    const an::Result<an::CatalogQueryResult> result = an::querySpectrumCatalogRegion(execution, catalog, query);
    ASSERT_TRUE(result.hasValue()) << result.status().message();
    ASSERT_EQ(result->entryIndices.size(), 4u);
    const std::vector<std::uint64_t> expected = {
        chunks[0].stars[0].sourceIdentifier, chunks[0].stars[3].sourceIdentifier, chunks[0].stars[1].sourceIdentifier,
        chunks[0].stars[2].sourceIdentifier};
    for (std::size_t index = 0u; index < expected.size(); ++index)
    {
        EXPECT_EQ(catalog.record(result->entryIndices[index])->sourceIdentifier, expected[index])
            << "position " << index;
    }
}

TEST(SpectrumCatalogTest, BuildRefusesDuplicatesForeignGridsAndMissizedTables)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    std::vector<FixtureChunk> chunks(2u);
    chunks[0].file = "a.anxp";
    chunks[0].firstTile = 0;
    chunks[0].tileCount = 24576;
    chunks[0].stars = {starAt(10.0, 5.0, 1u)};
    chunks[1].file = "b.anxp";
    chunks[1].firstTile = 24576;
    chunks[1].tileCount = 24576;
    chunks[1].stars = {starAt(200.0, -40.0, 1u)};
    chunks[1].stars[0].sourceIdentifier = chunks[0].stars[0].sourceIdentifier;
    const an::SpectrumCatalogManifest manifest = parseOrFail(execution, chunks);
    std::vector<an::SpectrumChunk> decoded;
    for (std::size_t index = 0u; index < 2u; ++index)
    {
        an::Result<an::SpectrumChunk> chunk = an::decodeSpectrumChunk(execution, makeChunkBytes(chunks[index], index),
                                                                      manifest, static_cast<std::uint32_t>(index));
        ASSERT_TRUE(chunk.hasValue()) << chunk.status().message();
        decoded.push_back(std::move(*chunk));
    }
    const an::Result<an::SpectrumCatalog> duplicate = an::buildSpectrumCatalog(execution, decoded, manifest);
    ASSERT_FALSE(duplicate.hasValue());
    EXPECT_EQ(duplicate.status().code(), an::ErrorCode::InvalidData);

    decoded[1].records[0].sourceIdentifier += 1u;
    std::vector<an::SpectrumChunk> repeated{decoded[0], decoded[0]};
    EXPECT_EQ(an::buildSpectrumCatalog(execution, repeated, manifest).status().code(), an::ErrorCode::InvalidData);
    std::vector<an::SpectrumChunk> foreign{decoded[0]};
    foreign[0].grid.stepNm = 3.0;
    EXPECT_EQ(an::buildSpectrumCatalog(execution, foreign, manifest).status().code(), an::ErrorCode::InvalidData);
    std::vector<an::SpectrumChunk> missized{decoded[0]};
    missized[0].flux.pop_back();
    EXPECT_EQ(an::buildSpectrumCatalog(execution, missized, manifest).status().code(), an::ErrorCode::InvalidData);
    std::vector<an::SpectrumChunk> outside{decoded[0]};
    outside[0].chunkIndex = 5;
    EXPECT_EQ(an::buildSpectrumCatalog(execution, outside, manifest).status().code(), an::ErrorCode::OutOfRange);
    std::vector<an::SpectrumChunk> zeroId{decoded[0]};
    zeroId[0].records[0].sourceIdentifier = 0u;
    EXPECT_EQ(an::buildSpectrumCatalog(execution, zeroId, manifest).status().code(), an::ErrorCode::InvalidData);

    const an::Result<an::SpectrumCatalog> distinct = an::buildSpectrumCatalog(execution, decoded, manifest);
    ASSERT_TRUE(distinct.hasValue()) << distinct.status().message();
    EXPECT_EQ(distinct->recordCount(), 2u);
    const an::Result<an::SpectrumCatalog> emptyCatalog = an::buildSpectrumCatalog(execution, {}, manifest);
    ASSERT_TRUE(emptyCatalog.hasValue());
    EXPECT_TRUE(emptyCatalog->valid());
    EXPECT_EQ(emptyCatalog->recordCount(), 0u);
    EXPECT_EQ(an::buildSpectrumCatalog(an::ExecutionContext{}, {}, manifest).status().code(),
              an::ErrorCode::NotInitialized);
}

TEST(SpectrumCatalogTest, ChunkSelectionFollowsTheTileRangesAndTheIdentifierBits)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::SkyCoordinate first{.rightAscensionDegrees = 12.0, .declinationDegrees = 30.0};
    const an::SkyCoordinate second{.rightAscensionDegrees = 200.0, .declinationDegrees = -55.0};
    const std::uint32_t tileA = tileSixOf(first.rightAscensionDegrees, first.declinationDegrees);
    const std::uint32_t tileB = tileSixOf(second.rightAscensionDegrees, second.declinationDegrees);
    ASSERT_NE(tileA, tileB);
    std::vector<FixtureChunk> chunks(2u);
    chunks[0].file = "a.anxp";
    chunks[0].firstTile = static_cast<std::int32_t>(tileA & ~63u);
    chunks[0].tileCount = 64;
    chunks[1].file = "b.anxp";
    chunks[1].firstTile = static_cast<std::int32_t>(tileB & ~63u);
    chunks[1].tileCount = 64;
    if (chunks[0].firstTile == chunks[1].firstTile)
    {
        chunks[1].firstTile += 64;
    }
    const an::SpectrumCatalogManifest manifest = parseOrFail(execution, chunks);

    const an::Result<an::SpectrumChunkSelection> nearFirst =
        an::selectSpectrumChunksForRegion(execution, manifest, first, 0.05);
    ASSERT_TRUE(nearFirst.hasValue()) << nearFirst.status().message();
    ASSERT_EQ(nearFirst->chunkIndices.size(), 1u);
    EXPECT_EQ(nearFirst->chunkIndices[0], 0u);
    EXPECT_GE(nearFirst->tilesExamined, 1u);
    const an::Result<an::SpectrumChunkSelection> wide =
        an::selectSpectrumChunksForRegion(execution, manifest, first, 40.0);
    ASSERT_TRUE(wide.hasValue()) << wide.status().message();
    EXPECT_GT(wide->undeclaredTiles, 0u);
    EXPECT_FALSE(wide->declaredCoverageComplete());
    EXPECT_EQ(an::selectSpectrumChunksForRegion(execution, manifest, first, -1.0).status().code(),
              an::ErrorCode::InvalidArgument);

    const std::vector<std::uint64_t> identifiers{
        identifierAt(first.rightAscensionDegrees, first.declinationDegrees, 3u),
        identifierAt(second.rightAscensionDegrees, second.declinationDegrees, 4u), 0u,
        identifierAt(first.rightAscensionDegrees, first.declinationDegrees, 9u)};
    const an::Result<std::vector<std::uint32_t>> bySource =
        an::selectSpectrumChunksForSources(execution, manifest, identifiers);
    ASSERT_TRUE(bySource.hasValue()) << bySource.status().message();
    ASSERT_EQ(bySource->size(), 2u);
    EXPECT_EQ((*bySource)[0], 0u);
    EXPECT_EQ((*bySource)[1], 1u);

    const an::SpectrumCoverageSummary partial = an::summarizeSpectrumCatalogCoverage(manifest);
    EXPECT_EQ(partial.declaredChunks, 2u);
    EXPECT_EQ(partial.expectedParentChunks, 768u);
    EXPECT_EQ(partial.declaredTiles, 128u);
    EXPECT_EQ(partial.expectedTiles, 49152u);
    EXPECT_FALSE(partial.fullSkyCoverage);

    const an::SpectrumCatalogManifest undivided = parseOrFail(execution, undividedChunks({starAt(1.0, 1.0, 1u)}));
    const an::Result<an::SpectrumChunkSelection> everything =
        an::selectSpectrumChunksForRegion(execution, undivided, second, 0.5);
    ASSERT_TRUE(everything.hasValue());
    ASSERT_EQ(everything->chunkIndices.size(), 1u);
    EXPECT_EQ(everything->chunkIndices[0], 0u);
    const an::Result<std::vector<std::uint32_t>> single =
        an::selectSpectrumChunksForSources(execution, undivided, identifiers);
    ASSERT_TRUE(single.hasValue());
    EXPECT_EQ(single->size(), 1u);
    EXPECT_FALSE(an::summarizeSpectrumCatalogCoverage(undivided).fullSkyCoverage);

    std::vector<FixtureChunk> full(768u);
    for (std::size_t index = 0u; index < full.size(); ++index)
    {
        full[index].file = "chunks/" + std::to_string(index) + ".anxp";
        full[index].firstTile = static_cast<std::int32_t>(index * 64u);
        full[index].tileCount = 64;
    }
    const an::SpectrumCatalogManifest fullSky = parseOrFail(execution, full);
    const an::SpectrumCoverageSummary complete = an::summarizeSpectrumCatalogCoverage(fullSky);
    EXPECT_EQ(complete.declaredChunks, 768u);
    EXPECT_EQ(complete.declaredTiles, 49152u);
    EXPECT_TRUE(complete.fullSkyCoverage);
    const an::Result<an::SpectrumChunkSelection> covered =
        an::selectSpectrumChunksForRegion(execution, fullSky, second, 3.0);
    ASSERT_TRUE(covered.hasValue());
    EXPECT_TRUE(covered->declaredCoverageComplete());
}

TEST(SpectrumCatalogTest, ReadinessIsDerivedFromTheManifestAndCheckedInOrder)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    ManifestOptions options;
    options.reviewStatus = "approved-for-managed-download";
    const an::SpectrumCatalogManifest manifest =
        parseOrFail(execution, undividedChunks({starAt(1.0, 1.0, 1u)}), options);
    an::SpectrumCatalogReadiness readiness = an::makeSpectrumCatalogReadiness(manifest, true);
    EXPECT_TRUE(readiness.rightsReviewApproved);
    EXPECT_TRUE(readiness.regionAvailable);
    EXPECT_EQ(readiness.manifestId, "manifest-test-001");
    EXPECT_EQ(an::checkSpectrumCatalogReadiness(readiness), an::SpectrumCatalogReadinessError::None);

    readiness.regionAvailable = false;
    EXPECT_EQ(an::checkSpectrumCatalogReadiness(readiness), an::SpectrumCatalogReadinessError::RegionNotCovered);
    readiness.rightsReviewApproved = false;
    EXPECT_EQ(an::checkSpectrumCatalogReadiness(readiness), an::SpectrumCatalogReadinessError::RightsReviewNotApproved);
    readiness.stale = true;
    EXPECT_EQ(an::checkSpectrumCatalogReadiness(readiness), an::SpectrumCatalogReadinessError::ChecksumInvalidOrStale);
    readiness.manifestId.clear();
    EXPECT_EQ(an::checkSpectrumCatalogReadiness(readiness), an::SpectrumCatalogReadinessError::MissingIdentity);
    readiness.accessProfile = an::SpectrumCatalogAccessProfile::ManagedRemote;
    EXPECT_EQ(an::checkSpectrumCatalogReadiness(readiness), an::SpectrumCatalogReadinessError::AccessProfileNotLocal);
    readiness.accessProfile.reset();
    EXPECT_EQ(an::checkSpectrumCatalogReadiness(readiness), an::SpectrumCatalogReadinessError::AccessProfileNotLocal);

    ManifestOptions pending = options;
    pending.reviewStatus = "pending";
    const an::SpectrumCatalogManifest unreviewed =
        parseOrFail(execution, undividedChunks({starAt(1.0, 1.0, 1u)}), pending);
    EXPECT_FALSE(an::makeSpectrumCatalogReadiness(unreviewed, true).rightsReviewApproved);
    const an::SpectrumCatalogReadiness unchecked = an::makeSpectrumCatalogReadiness(manifest, false, true, false);
    EXPECT_EQ(an::checkSpectrumCatalogReadiness(unchecked), an::SpectrumCatalogReadinessError::ChecksumInvalidOrStale);
    EXPECT_STREQ(an::spectrumCatalogReadinessErrorName(an::SpectrumCatalogReadinessError::RegionNotCovered),
                 "RegionNotCovered");
    EXPECT_FALSE(an::isSpectrumCatalogReadinessError(static_cast<an::SpectrumCatalogReadinessError>(6u)));
}

TEST(SpectrumCatalogTest, RecordedMatchesAreResolvedAgainstTheCatalogInAFixedOrder)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    FixtureSpectrumStar clean = starAt(50.0, 20.0, 1u);
    FixtureSpectrumStar flagged = starAt(50.1, 20.1, 2u);
    flagged.qualityFlags = 3u;
    const std::vector<FixtureChunk> chunks = undividedChunks({clean, flagged});
    const an::SpectrumCatalog catalog = spectrum_test::buildOrFail(execution, chunks);

    an::MatchedSourceReference good;
    good.sourceIdentifier = clean.sourceIdentifier;
    good.spectrumCatalogFamilyId = an::kXpCatalogFamilyId;
    good.spectrumCatalogManifestId = "manifest-test-001";
    good.astrometricCatalogFamilyId = "gaia_dr3";
    good.observationEpoch = "2026.0";
    good.residualPixels = 0.4;
    good.separationArcseconds = 0.9;
    an::MatchedSourceReference invalid = good;
    invalid.sourceIdentifier = 0u;
    an::MatchedSourceReference missing = good;
    missing.sourceIdentifier = 424242u;
    an::MatchedSourceReference otherManifest = good;
    otherManifest.spectrumCatalogManifestId = "manifest-test-002";
    an::MatchedSourceReference stale = good;
    stale.observationEpoch = "2020.0";
    an::MatchedSourceReference farAway = good;
    farAway.residualPixels = 5.0;
    an::MatchedSourceReference unmeasured = good;
    unmeasured.separationArcseconds.reset();
    an::MatchedSourceReference flaggedReference = good;
    flaggedReference.sourceIdentifier = flagged.sourceIdentifier;
    const std::vector<an::MatchedSourceReference> references{good,  invalid, missing,    otherManifest,
                                                             stale, farAway, unmeasured, flaggedReference};

    an::MatchedSourceRequirements requirements;
    requirements.requiredObservationEpoch = "2026.0";
    requirements.maximumResidualPixels = 2.0;
    requirements.maximumSeparationArcseconds = 2.0;
    const an::Result<an::MatchedSourceResolution> resolved =
        an::resolveMatchedSources(execution, references, catalog, requirements);
    ASSERT_TRUE(resolved.hasValue()) << resolved.status().message();
    ASSERT_EQ(resolved->lookups.size(), 8u);
    EXPECT_EQ(resolved->lookups[0].status, an::MatchedSourceLookupStatus::Satisfied);
    ASSERT_TRUE(resolved->lookups[0].recordIndex.has_value());
    EXPECT_EQ(catalog.record(*resolved->lookups[0].recordIndex)->sourceIdentifier, clean.sourceIdentifier);
    EXPECT_EQ(resolved->lookups[1].status, an::MatchedSourceLookupStatus::InvalidSourceIdentifier);
    EXPECT_EQ(resolved->lookups[2].status, an::MatchedSourceLookupStatus::MissingSource);
    EXPECT_EQ(resolved->lookups[3].status, an::MatchedSourceLookupStatus::ManifestMismatch);
    EXPECT_EQ(resolved->lookups[4].status, an::MatchedSourceLookupStatus::StaleMatch);
    EXPECT_EQ(resolved->lookups[5].status, an::MatchedSourceLookupStatus::StaleMatch);
    EXPECT_EQ(resolved->lookups[6].status, an::MatchedSourceLookupStatus::StaleMatch);
    EXPECT_EQ(resolved->lookups[7].status, an::MatchedSourceLookupStatus::RejectedQualityFlags);
    EXPECT_FALSE(resolved->lookups[7].recordIndex.has_value());
    EXPECT_EQ(resolved->acceptedCount, 1u);
    EXPECT_EQ(resolved->rejectedCount, 7u);
    EXPECT_EQ(resolved->invalidSourceIdentifierCount, 1u);
    EXPECT_EQ(resolved->missingSourceCount, 1u);
    EXPECT_EQ(resolved->manifestMismatchCount, 1u);
    EXPECT_EQ(resolved->staleMatchCount, 3u);
    EXPECT_EQ(resolved->qualityRejectedCount, 1u);
    EXPECT_EQ(resolved->cacheMissCount, 0u);

    requirements.rejectNonZeroQualityFlags = false;
    const an::Result<an::MatchedSourceResolution> lenient =
        an::resolveMatchedSources(execution, references, catalog, requirements);
    ASSERT_TRUE(lenient.hasValue());
    EXPECT_EQ(lenient->lookups[7].status, an::MatchedSourceLookupStatus::Satisfied);

    ManifestOptions regional;
    regional.accessProfile = "installed-regional-cache";
    const an::SpectrumCatalog cache = spectrum_test::buildOrFail(execution, chunks, regional);
    const an::Result<an::MatchedSourceResolution> cached =
        an::resolveMatchedSources(execution, std::vector<an::MatchedSourceReference>{missing}, cache);
    ASSERT_TRUE(cached.hasValue());
    EXPECT_EQ(cached->lookups[0].status, an::MatchedSourceLookupStatus::MissingChunkOrCache);
    EXPECT_EQ(cached->cacheMissCount, 1u);

    requirements.maximumResidualPixels = std::numeric_limits<double>::infinity();
    EXPECT_EQ(an::resolveMatchedSources(execution, references, catalog, requirements).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::resolveMatchedSources(execution, references, an::SpectrumCatalog{}).status().code(),
              an::ErrorCode::NotInitialized);
    EXPECT_STREQ(an::matchedSourceLookupStatusName(an::MatchedSourceLookupStatus::StaleMatch), "StaleMatch");
    EXPECT_FALSE(an::isMatchedSourceLookupStatus(static_cast<an::MatchedSourceLookupStatus>(7u)));
}

} // namespace

TEST(SpectrumCatalogTest, ChunkArrayDistinguishesMalformedDataFromTheEntryLimit)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const std::vector<FixtureChunk> chunks(2u);
    const auto text = makeManifestText(chunks);
    an::SpectrumManifestLimits limits;
    limits.maximumChunks = 1u;
    const auto limited = an::parseSpectrumCatalogManifest(execution, text, limits);
    ASSERT_EQ(limited.code(), an::ErrorCode::LimitExceeded);
    ASSERT_NE(limited.status().findContext(an::ContextKey::Requested), nullptr);
    EXPECT_EQ(limited.status().findContext(an::ContextKey::Requested)->count, 2u);
    auto malformed = text;
    const auto at = malformed.find("\"chunks\"");
    ASSERT_NE(at, std::string::npos);
    malformed.erase(malformed.find('[', at) + 1u);
    malformed += "true,,false]}";
    EXPECT_EQ(an::parseSpectrumCatalogManifest(execution, malformed).code(), an::ErrorCode::InvalidData);
}
