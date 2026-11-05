// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What a Gaia catalog says about itself, and the tile index its chunk list has to be.

#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogManifest.h>

#include "CatalogTestSupport.h"
#include "GaiaTestSupport.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace
{

using catalog_test::makeEngine;
using catalog_test::makeExecution;
using gaia_test::contextCount;
using gaia_test::makeChunkManifestText;

/// One chunk description, spelled at the call site.
[[nodiscard]] an::GaiaChunkDescriptor makeChunk(std::int32_t index, std::string file, std::int32_t firstTile,
                                                std::int32_t tileCount, std::uint64_t records)
{
    an::GaiaChunkDescriptor chunk;
    chunk.index = index;
    chunk.file = std::move(file);
    chunk.firstTile = firstTile;
    chunk.tileCount = tileCount;
    chunk.recordCount = records;
    chunk.sizeBytes = 0u;
    chunk.checksum = "abc123";
    return chunk;
}

/// A two-chunk manifest covering four tiles.
[[nodiscard]] std::vector<an::GaiaChunkDescriptor> twoChunks()
{
    return {makeChunk(0, "chunks/chunk_00000.ansp", 0, 2, 5u), makeChunk(1, "chunks/chunk_00001.ansp", 2, 2, 7u)};
}

/// The manifest a text parses to, failing the test rather than the caller.
[[nodiscard]] an::ParsedGaiaManifest parseOrFail(const an::ExecutionContext& execution, const std::string& text)
{
    an::Result<an::ParsedGaiaManifest> parsed = an::parseGaiaCatalogManifest(execution, text);
    EXPECT_TRUE(parsed.hasValue()) << parsed.status().message();
    return parsed.hasValue() ? std::move(*parsed) : an::ParsedGaiaManifest{};
}

TEST(GaiaCatalogManifestTest, TheStorageVocabularyIsAClosedPair)
{
    EXPECT_TRUE(an::isGaiaCatalogStorage(an::GaiaCatalogStorage::Tiles));
    EXPECT_TRUE(an::isGaiaCatalogStorage(an::GaiaCatalogStorage::Chunks));
    EXPECT_FALSE(an::isGaiaCatalogStorage(static_cast<an::GaiaCatalogStorage>(7)));
    EXPECT_STREQ(an::gaiaCatalogStorageName(an::GaiaCatalogStorage::Chunks), "Chunks");
    EXPECT_STREQ(an::gaiaCatalogStorageName(static_cast<an::GaiaCatalogStorage>(7)), "UnknownGaiaCatalogStorage");
}

TEST(GaiaCatalogManifestTest, AnOmittedMemberFallsBackToWhatTheFormatDefaultsItTo)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ParsedGaiaManifest parsed = parseOrFail(execution, "{}");

    EXPECT_FALSE(parsed.statedTotalRecordCount);
    EXPECT_EQ(parsed.metadata.formatVersion, 1);
    EXPECT_EQ(parsed.metadata.storage, an::GaiaCatalogStorage::Tiles);
    EXPECT_EQ(parsed.metadata.recordSizeBytes, 36u);
    EXPECT_EQ(parsed.metadata.recordLayout, an::GaiaRecordLayout::Version1);
    EXPECT_EQ(parsed.metadata.pixelisationOrder, 5);
    EXPECT_EQ(parsed.metadata.pixelOrdering, "ring");
    EXPECT_EQ(parsed.metadata.catalogName, "Gaia DR3 Local Catalog");
    EXPECT_EQ(parsed.metadata.catalogIdentifier, "gaia_dr3");
    EXPECT_DOUBLE_EQ(parsed.metadata.epochYear, 2016.0);
    EXPECT_DOUBLE_EQ(parsed.metadata.magnitudeLimit, 14.0);
    EXPECT_EQ(parsed.metadata.totalRecordCount, 0u);
    EXPECT_EQ(parsed.metadata.tileDirectory, "healpix_order5");
    EXPECT_TRUE(parsed.metadata.chunks.empty());
}

TEST(GaiaCatalogManifestTest, TheSecondTileFormatDefaultsToTheRecordThatCarriesAnIdentifier)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ParsedGaiaManifest parsed =
        parseOrFail(execution, "{\"format_version\":2,\"healpix_order\":6,\"total_stars\":1234}");

    EXPECT_EQ(parsed.metadata.recordSizeBytes, 44u);
    EXPECT_EQ(parsed.metadata.recordLayout, an::GaiaRecordLayout::Version2);
    EXPECT_EQ(parsed.metadata.tileDirectory, "healpix_order6");
    EXPECT_TRUE(parsed.statedTotalRecordCount);
    EXPECT_EQ(parsed.metadata.totalRecordCount, 1234u);
}

TEST(GaiaCatalogManifestTest, ParsingRefusesWhatItCannotRead)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    const an::ExecutionContext none;
    EXPECT_EQ(an::parseGaiaCatalogManifest(none, "{}").status().code(), an::ErrorCode::NotInitialized);

    an::GaiaManifestLimits nothing;
    nothing.maximumBytes = 0u;
    EXPECT_EQ(an::parseGaiaCatalogManifest(execution, "{}", nothing).status().code(), an::ErrorCode::InvalidArgument);

    an::GaiaManifestLimits tiny;
    tiny.maximumBytes = 1u;
    const an::Status oversize = an::parseGaiaCatalogManifest(execution, "{}", tiny).status();
    EXPECT_EQ(oversize.code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(contextCount(oversize, an::ContextKey::Requested), 2u);
    EXPECT_EQ(contextCount(oversize, an::ContextKey::Limit), 1u);

    for (const char* text : {"", "not a document", "[]", "{", "{\"storage\"}", "{\"a\":1}{\"b\":2}"})
    {
        EXPECT_EQ(an::parseGaiaCatalogManifest(execution, text).status().code(), an::ErrorCode::InvalidData) << text;
    }
    EXPECT_EQ(an::parseGaiaCatalogManifest(execution, "{\"format_version\":\"three\"}").status().code(),
              an::ErrorCode::InvalidData);
    EXPECT_EQ(an::parseGaiaCatalogManifest(execution, "{\"catalog_id\":7}").status().code(),
              an::ErrorCode::InvalidData);
    EXPECT_EQ(an::parseGaiaCatalogManifest(execution, "{\"healpix_order\":-1}").status().code(),
              an::ErrorCode::InvalidData);
}

TEST(GaiaCatalogManifestTest, AnArrangementOrNumberingThisBuildDoesNotReadIsRefusedByName)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    // Reading every storage name that is not the chunk one as tile storage would turn a manifest with
    // a spelling mistake in it into a tile catalog with no tiles in it.
    EXPECT_EQ(an::parseGaiaCatalogManifest(execution, "{\"storage\":\"chunk\"}").status().code(),
              an::ErrorCode::Unsupported);
    EXPECT_EQ(an::parseGaiaCatalogManifest(execution, "{\"pixel_ordering\":\"nested\"}").status().code(),
              an::ErrorCode::Unsupported);
    EXPECT_EQ(an::parseGaiaCatalogManifest(execution, "{\"record_size_bytes\":48}").status().code(),
              an::ErrorCode::Unsupported);
    EXPECT_EQ(an::parseGaiaCatalogManifest(execution, "{\"record_size_bytes\":64}").status().code(),
              an::ErrorCode::Unsupported);
    EXPECT_EQ(an::parseGaiaCatalogManifest(execution, "{\"tile_directory\":\"../elsewhere\"}").status().code(),
              an::ErrorCode::InvalidData);
}

TEST(GaiaCatalogManifestTest, AChunkedCatalogMustBeTheFormatChunksAreDefinedAt)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    EXPECT_EQ(an::parseGaiaCatalogManifest(execution, "{\"storage\":\"chunks\",\"format_version\":2}").status().code(),
              an::ErrorCode::InvalidData);
    EXPECT_EQ(an::parseGaiaCatalogManifest(execution,
                                           "{\"storage\":\"chunks\",\"format_version\":3,\"record_size_bytes\":44}")
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);
}

TEST(GaiaCatalogManifestTest, TheChunkListIsReadWithEveryFieldItStates)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<an::GaiaChunkDescriptor> chunks = twoChunks();
    const an::ParsedGaiaManifest parsed = parseOrFail(execution, makeChunkManifestText(chunks));

    EXPECT_EQ(parsed.metadata.storage, an::GaiaCatalogStorage::Chunks);
    EXPECT_EQ(parsed.metadata.recordLayout, an::GaiaRecordLayout::Version3);
    EXPECT_TRUE(parsed.metadata.tileDirectory.empty());
    ASSERT_EQ(parsed.metadata.chunks.size(), 2u);
    EXPECT_EQ(parsed.metadata.chunks[0], chunks[0]);
    EXPECT_EQ(parsed.metadata.chunks[1], chunks[1]);
}

TEST(GaiaCatalogManifestTest, AStatedChunkCountMustAgreeWithTheChunkList)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<an::GaiaChunkDescriptor> chunks = twoChunks();

    const an::ParsedGaiaManifest agreeing = parseOrFail(execution, makeChunkManifestText(chunks, "\"chunk_count\":2"));
    EXPECT_EQ(agreeing.metadata.chunks.size(), 2u);

    EXPECT_EQ(
        an::parseGaiaCatalogManifest(execution, makeChunkManifestText(chunks, "\"chunk_count\":3")).status().code(),
        an::ErrorCode::InvalidData);
    EXPECT_EQ(an::parseGaiaCatalogManifest(execution, "{\"storage\":\"chunks\",\"format_version\":3,\"chunks\":7}")
                  .status()
                  .code(),
              an::ErrorCode::InvalidData);
}

TEST(GaiaCatalogManifestTest, MoreChunksThanTheLimitAdmitsIsARefusal)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::GaiaManifestLimits limits;
    limits.maximumChunks = 1u;
    EXPECT_EQ(an::parseGaiaCatalogManifest(execution, makeChunkManifestText(twoChunks()), limits).status().code(),
              an::ErrorCode::LimitExceeded);
}

TEST(GaiaCatalogManifestTest, ParsingStopsWhenTheCallerAsksItTo)
{
    const an::EngineContext engine = makeEngine();
    std::stop_source stop;
    const an::ExecutionContext execution = makeExecution(engine, 4u, stop.get_token());
    stop.request_stop();
    EXPECT_EQ(an::parseGaiaCatalogManifest(execution, makeChunkManifestText(twoChunks())).status().code(),
              an::ErrorCode::Cancelled);
}

TEST(GaiaCatalogManifestTest, TheTileMapCoversTheSkyAndAddsUpTheChunks)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ParsedGaiaManifest parsed = parseOrFail(execution, makeChunkManifestText(twoChunks()));

    an::GaiaTileMapRequest request;
    request.skyTileCount = 4u;
    an::Result<an::GaiaTileMap> map = an::buildGaiaTileMap(execution, parsed.metadata, request);
    ASSERT_TRUE(map.hasValue()) << map.status().message();
    EXPECT_EQ(map->tileChunkIndices, (std::vector<std::uint32_t>{0u, 0u, 1u, 1u}));
    EXPECT_EQ(map->totalRecordCount, 12u);
}

TEST(GaiaCatalogManifestTest, TheTileMapRefusesAChunkListThatIsNotAnIndexOverTheSky)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    an::GaiaTileMapRequest request;
    request.skyTileCount = 4u;

    const an::ParsedGaiaManifest good = parseOrFail(execution, makeChunkManifestText(twoChunks()));
    const an::ExecutionContext none;
    EXPECT_EQ(an::buildGaiaTileMap(none, good.metadata, request).status().code(), an::ErrorCode::NotInitialized);

    an::GaiaTileMapRequest empty;
    empty.skyTileCount = 0u;
    EXPECT_EQ(an::buildGaiaTileMap(execution, good.metadata, empty).status().code(), an::ErrorCode::InvalidArgument);

    const an::ParsedGaiaManifest tiles = parseOrFail(execution, "{}");
    EXPECT_EQ(an::buildGaiaTileMap(execution, tiles.metadata, request).status().code(), an::ErrorCode::Unsupported);

    const auto refuses = [&](std::vector<an::GaiaChunkDescriptor> chunks, const char* what) {
        const an::ParsedGaiaManifest broken = parseOrFail(execution, makeChunkManifestText(chunks));
        EXPECT_EQ(an::buildGaiaTileMap(execution, broken.metadata, request).status().code(), an::ErrorCode::InvalidData)
            << what;
    };

    refuses({}, "no chunks at all");
    refuses({makeChunk(1, "a.ansp", 0, 4, 1u)}, "positions that do not start at zero");
    refuses({makeChunk(0, "", 0, 4, 1u)}, "a chunk that names no file");
    refuses({makeChunk(0, "/elsewhere/a.ansp", 0, 4, 1u)}, "an absolute path");
    refuses({makeChunk(0, "../a.ansp", 0, 4, 1u)}, "a path that steps out of the catalog");
    refuses({makeChunk(0, "a.ansp", 0, 2, 1u), makeChunk(1, "a.ansp", 2, 2, 1u)}, "two chunks in one file");
    refuses({makeChunk(0, "a.ansp", 0, 0, 0u), makeChunk(1, "b.ansp", 0, 4, 1u)}, "a chunk holding no tiles");
    refuses({makeChunk(0, "a.ansp", 0, 5, 1u)}, "a range reaching outside the sky");
    refuses({makeChunk(0, "a.ansp", 0, 3, 1u), makeChunk(1, "b.ansp", 2, 2, 1u)}, "overlapping ranges");
    refuses({makeChunk(0, "a.ansp", 0, 2, 1u)}, "a sky the chunks do not cover");
}

TEST(GaiaCatalogManifestTest, ATilePathIsTheProvidersOwnFileNamingRule)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const an::ParsedGaiaManifest parsed = parseOrFail(execution, "{}");

    EXPECT_EQ(an::makeGaiaTilePath(parsed.metadata, 0), "healpix_order5/tile_00000.bin");
    EXPECT_EQ(an::makeGaiaTilePath(parsed.metadata, 1234), "healpix_order5/tile_01234.bin");
    EXPECT_EQ(an::makeGaiaTilePath(parsed.metadata, 123456), "healpix_order5/tile_123456.bin");
    EXPECT_TRUE(an::makeGaiaTilePath(parsed.metadata, -1).empty());

    an::GaiaCatalogMetadata flat;
    flat.tileDirectory.clear();
    EXPECT_EQ(an::makeGaiaTilePath(flat, 7), "tile_00007.bin");
}

} // namespace

TEST(GaiaCatalogManifestTest, DotsInsideAFileNameAreNotParentTraversal)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    for (const std::string path : {"chunks/chunk..data.ansp", "chunks/../data.ansp", "chunks\\..\\data.ansp"})
    {
        auto parsed = parseOrFail(execution, makeChunkManifestText(twoChunks()));
        parsed.metadata.chunks[0].file = path;
        an::GaiaTileMapRequest request;
        request.skyTileCount = 4u;
        const auto map = an::buildGaiaTileMap(execution, parsed.metadata, request);
        EXPECT_EQ(map.hasValue(), path == "chunks/chunk..data.ansp") << path;
    }
}
