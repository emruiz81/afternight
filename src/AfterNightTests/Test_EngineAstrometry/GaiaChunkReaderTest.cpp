// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The container a chunked Gaia catalog stores its tiles in, and everything it refuses to be.

#include <AfterNightEngine/Algorithms/Catalog/GaiaChunkReader.h>

#include "CatalogTestSupport.h"
#include "GaiaTestSupport.h"

#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogManifest.h>
#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogRecord.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace
{

using catalog_test::makeEngine;
using catalog_test::makeExecution;
using gaia_test::ChunkFixtureOptions;
using gaia_test::chunkIndexOffset;
using gaia_test::FixtureStar;
using gaia_test::FixtureTile;
using gaia_test::makeChunkFile;
using gaia_test::patchUnsigned;

/// One star at a stated place, so a decoded tile can be told from another.
[[nodiscard]] FixtureStar starAt(double rightAscensionDegrees, std::uint64_t sourceIdentifier)
{
    FixtureStar star;
    star.rightAscensionDegrees = rightAscensionDegrees;
    star.declinationDegrees = 12.5;
    star.sourceIdentifier = sourceIdentifier;
    star.magnitudeG = 10.5f;
    return star;
}

/// Three tiles of two, none and one star.
[[nodiscard]] std::vector<FixtureTile> threeTiles()
{
    std::vector<FixtureTile> tiles(3u);
    tiles[0].stars = {starAt(10.0, 101u), starAt(11.0, 102u)};
    tiles[2].stars = {starAt(12.0, 103u)};
    return tiles;
}

/// The catalog description a fixture chunk belongs to.
[[nodiscard]] an::GaiaCatalogMetadata makeMetadata(std::uint64_t recordCount, std::int32_t tileCount)
{
    an::GaiaCatalogMetadata metadata;
    metadata.formatVersion = 3;
    metadata.storage = an::GaiaCatalogStorage::Chunks;
    metadata.recordSizeBytes = 64u;
    metadata.recordLayout = an::GaiaRecordLayout::Version3;
    metadata.pixelisationOrder = 5;
    metadata.pixelOrdering = "ring";
    metadata.catalogIdentifier = "gaia_dr3";
    metadata.tileDirectory.clear();

    an::GaiaChunkDescriptor chunk;
    chunk.index = 0;
    chunk.file = "chunks/chunk_00000.ansp";
    chunk.firstTile = 0;
    chunk.tileCount = tileCount;
    chunk.recordCount = recordCount;
    metadata.chunks.push_back(chunk);
    return metadata;
}

/// The reason a fixture chunk written a stated way is refused.
[[nodiscard]] an::ErrorCode refusalFor(const an::ExecutionContext& execution, const ChunkFixtureOptions& options)
{
    const std::vector<FixtureTile> tiles = threeTiles();
    const std::vector<std::byte> bytes = makeChunkFile(tiles, options);
    const an::GaiaCatalogMetadata metadata = makeMetadata(3u, 3);
    return an::readGaiaChunkLayout(execution, bytes, metadata, metadata.chunks.front()).status().code();
}

TEST(GaiaChunkReaderTest, TheContainerAndItsIndexAreReadAsWritten)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<FixtureTile> tiles = threeTiles();
    const std::vector<std::byte> bytes = makeChunkFile(tiles);
    const an::GaiaCatalogMetadata metadata = makeMetadata(3u, 3);

    an::Result<an::GaiaChunkLayout> layout =
        an::readGaiaChunkLayout(execution, bytes, metadata, metadata.chunks.front());
    ASSERT_TRUE(layout.hasValue()) << layout.status().message();
    EXPECT_EQ(layout->chunkIndex, 0);
    EXPECT_EQ(layout->firstTile, 0);
    EXPECT_EQ(layout->recordLayout, an::GaiaRecordLayout::Version3);
    EXPECT_EQ(layout->totalRecordCount, 3u);
    ASSERT_EQ(layout->tiles.size(), 3u);
    EXPECT_EQ(layout->tiles[0], (an::GaiaChunkTileSpan{.recordCount = 2u, .recordAreaOffset = 0u}));
    EXPECT_EQ(layout->tiles[1], (an::GaiaChunkTileSpan{.recordCount = 0u, .recordAreaOffset = 128u}));
    EXPECT_EQ(layout->tiles[2], (an::GaiaChunkTileSpan{.recordCount = 1u, .recordAreaOffset = 128u}));
    EXPECT_EQ(layout->recordAreaOffset + 192u, bytes.size());
}

TEST(GaiaChunkReaderTest, ATilesBytesDecodeToTheStarsItWasWrittenWith)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<FixtureTile> tiles = threeTiles();
    const std::vector<std::byte> bytes = makeChunkFile(tiles);
    const an::GaiaCatalogMetadata metadata = makeMetadata(3u, 3);
    an::Result<an::GaiaChunkLayout> layout =
        an::readGaiaChunkLayout(execution, bytes, metadata, metadata.chunks.front());
    ASSERT_TRUE(layout.hasValue()) << layout.status().message();

    an::Result<std::span<const std::byte>> first = an::gaiaChunkTileRecordBytes(bytes, *layout, 0);
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    EXPECT_EQ(first->size(), 128u);
    EXPECT_EQ(first->data(), bytes.data() + layout->recordAreaOffset);

    an::Result<std::vector<an::GaiaStarRecord>> records =
        an::decodeGaiaRecords(execution, *first, layout->recordLayout);
    ASSERT_TRUE(records.hasValue()) << records.status().message();
    ASSERT_EQ(records->size(), 2u);
    EXPECT_EQ((*records)[0].sourceIdentifier, 101u);
    EXPECT_DOUBLE_EQ((*records)[1].coordinate.rightAscensionDegrees, 11.0);

    an::Result<std::span<const std::byte>> empty = an::gaiaChunkTileRecordBytes(bytes, *layout, 1);
    ASSERT_TRUE(empty.hasValue()) << empty.status().message();
    EXPECT_TRUE(empty->empty());

    an::Result<std::span<const std::byte>> last = an::gaiaChunkTileRecordBytes(bytes, *layout, 2);
    ASSERT_TRUE(last.hasValue()) << last.status().message();
    an::Result<std::vector<an::GaiaStarRecord>> lastRecords =
        an::decodeGaiaRecords(execution, *last, layout->recordLayout);
    ASSERT_TRUE(lastRecords.hasValue()) << lastRecords.status().message();
    ASSERT_EQ(lastRecords->size(), 1u);
    EXPECT_EQ(lastRecords->front().sourceIdentifier, 103u);
}

TEST(GaiaChunkReaderTest, ATileOutsideTheChunkOrABytesMismatchIsRefused)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<FixtureTile> tiles = threeTiles();
    const std::vector<std::byte> bytes = makeChunkFile(tiles);
    const an::GaiaCatalogMetadata metadata = makeMetadata(3u, 3);
    an::Result<an::GaiaChunkLayout> layout =
        an::readGaiaChunkLayout(execution, bytes, metadata, metadata.chunks.front());
    ASSERT_TRUE(layout.hasValue()) << layout.status().message();

    EXPECT_EQ(an::gaiaChunkTileRecordBytes(bytes, *layout, -1).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::gaiaChunkTileRecordBytes(bytes, *layout, 3).status().code(), an::ErrorCode::OutOfRange);

    const std::span<const std::byte> shorter{bytes.data(), bytes.size() - 64u};
    EXPECT_EQ(an::gaiaChunkTileRecordBytes(shorter, *layout, 2).status().code(), an::ErrorCode::InvalidData);
}

TEST(GaiaChunkReaderTest, ReadingRefusesWhatIsNotAChunkOfThisCatalog)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<FixtureTile> tiles = threeTiles();
    const std::vector<std::byte> bytes = makeChunkFile(tiles);
    const an::GaiaCatalogMetadata metadata = makeMetadata(3u, 3);

    const an::ExecutionContext none;
    EXPECT_EQ(an::readGaiaChunkLayout(none, bytes, metadata, metadata.chunks.front()).status().code(),
              an::ErrorCode::NotInitialized);

    an::GaiaCatalogMetadata tileStored = metadata;
    tileStored.storage = an::GaiaCatalogStorage::Tiles;
    EXPECT_EQ(an::readGaiaChunkLayout(execution, bytes, tileStored, metadata.chunks.front()).status().code(),
              an::ErrorCode::Unsupported);

    an::GaiaChunkLimits nothing;
    nothing.maximumTiles = 0u;
    EXPECT_EQ(an::readGaiaChunkLayout(execution, bytes, metadata, metadata.chunks.front(), nothing).status().code(),
              an::ErrorCode::InvalidArgument);

    an::GaiaChunkDescriptor stranger = metadata.chunks.front();
    stranger.file = "chunks/somewhere_else.ansp";
    EXPECT_EQ(an::readGaiaChunkLayout(execution, bytes, metadata, stranger).status().code(),
              an::ErrorCode::InvalidArgument);

    an::GaiaCatalogMetadata declaredSize = metadata;
    declaredSize.chunks.front().sizeBytes = bytes.size() + 1u;
    EXPECT_EQ(an::readGaiaChunkLayout(execution, bytes, declaredSize, declaredSize.chunks.front()).status().code(),
              an::ErrorCode::InvalidData);

    const std::span<const std::byte> stub{bytes.data(), 8u};
    EXPECT_EQ(an::readGaiaChunkLayout(execution, stub, metadata, metadata.chunks.front()).status().code(),
              an::ErrorCode::InvalidData);
}

TEST(GaiaChunkReaderTest, ThePreambleIsCheckedBeforeAnythingIsBelieved)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    ChunkFixtureOptions wrongMagic;
    wrongMagic.magic = "ANSQ";
    EXPECT_EQ(refusalFor(execution, wrongMagic), an::ErrorCode::InvalidData);

    ChunkFixtureOptions futureContainer;
    futureContainer.containerVersion = 2u;
    EXPECT_EQ(refusalFor(execution, futureContainer), an::ErrorCode::Unsupported);

    ChunkFixtureOptions flagged;
    flagged.flags = 1u;
    EXPECT_EQ(refusalFor(execution, flagged), an::ErrorCode::Unsupported);

    ChunkFixtureOptions reserved;
    reserved.reserved = 1u;
    EXPECT_EQ(refusalFor(execution, reserved), an::ErrorCode::Unsupported);

    ChunkFixtureOptions overlongHeader;
    overlongHeader.statedHeaderLength = 1u << 20u;
    EXPECT_EQ(refusalFor(execution, overlongHeader), an::ErrorCode::InvalidData);
}

TEST(GaiaChunkReaderTest, AHeaderLargerThanTheLimitIsARefusal)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<FixtureTile> tiles = threeTiles();
    const std::vector<std::byte> bytes = makeChunkFile(tiles);
    const an::GaiaCatalogMetadata metadata = makeMetadata(3u, 3);

    an::GaiaChunkLimits limits;
    limits.maximumHeaderBytes = 4u;
    EXPECT_EQ(an::readGaiaChunkLayout(execution, bytes, metadata, metadata.chunks.front(), limits).status().code(),
              an::ErrorCode::LimitExceeded);
}

TEST(GaiaChunkReaderTest, TheHeaderMustStateEverythingAChunkStatesAndAgreeWithIt)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);

    ChunkFixtureOptions malformed;
    malformed.headerTextOverride = std::string{"not an object"};
    EXPECT_EQ(refusalFor(execution, malformed), an::ErrorCode::InvalidData);

    ChunkFixtureOptions incomplete;
    incomplete.headerTextOverride = std::string{"{\"chunk_index\":0}"};
    EXPECT_EQ(refusalFor(execution, incomplete), an::ErrorCode::InvalidData);

    ChunkFixtureOptions otherCatalog;
    otherCatalog.catalogIdentifier = "gaia_dr4";
    EXPECT_EQ(refusalFor(execution, otherCatalog), an::ErrorCode::InvalidData);

    ChunkFixtureOptions otherOrdering;
    otherOrdering.pixelOrdering = "nested";
    EXPECT_EQ(refusalFor(execution, otherOrdering), an::ErrorCode::InvalidData);

    ChunkFixtureOptions otherOrder;
    otherOrder.pixelisationOrder = 6;
    EXPECT_EQ(refusalFor(execution, otherOrder), an::ErrorCode::InvalidData);

    ChunkFixtureOptions otherRecordVersion;
    otherRecordVersion.recordVersion = 2;
    EXPECT_EQ(refusalFor(execution, otherRecordVersion), an::ErrorCode::InvalidData);

    ChunkFixtureOptions otherRecordSize;
    otherRecordSize.recordSizeBytes = 44;
    EXPECT_EQ(refusalFor(execution, otherRecordSize), an::ErrorCode::InvalidData);

    ChunkFixtureOptions otherIndex;
    otherIndex.chunkIndex = 1;
    EXPECT_EQ(refusalFor(execution, otherIndex), an::ErrorCode::InvalidData);

    ChunkFixtureOptions otherFirstTile;
    otherFirstTile.firstTile = 4;
    EXPECT_EQ(refusalFor(execution, otherFirstTile), an::ErrorCode::InvalidData);

    ChunkFixtureOptions otherTileCount;
    otherTileCount.statedTileCount = 2;
    EXPECT_EQ(refusalFor(execution, otherTileCount), an::ErrorCode::InvalidData);

    ChunkFixtureOptions otherChunkCount;
    otherChunkCount.chunkCount = 2;
    EXPECT_EQ(refusalFor(execution, otherChunkCount), an::ErrorCode::InvalidData);

    ChunkFixtureOptions otherRecordCount;
    otherRecordCount.statedRecordCount = 4u;
    EXPECT_EQ(refusalFor(execution, otherRecordCount), an::ErrorCode::InvalidData);
}

TEST(GaiaChunkReaderTest, AChunkThatHoldsNoTilesIsRefused)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<FixtureTile> none;
    const std::vector<std::byte> bytes = makeChunkFile(none);
    const an::GaiaCatalogMetadata metadata = makeMetadata(0u, 0);
    EXPECT_EQ(an::readGaiaChunkLayout(execution, bytes, metadata, metadata.chunks.front()).status().code(),
              an::ErrorCode::InvalidData);
}

TEST(GaiaChunkReaderTest, TheTileIndexItselfIsCheckedAgainstTheBytesItPointsInto)
{
    const an::EngineContext engine = makeEngine();
    const an::ExecutionContext execution = makeExecution(engine);
    const std::vector<FixtureTile> tiles = threeTiles();
    const an::GaiaCatalogMetadata metadata = makeMetadata(3u, 3);
    const std::vector<std::byte> good = makeChunkFile(tiles);
    const std::size_t indexOffset = chunkIndexOffset(good);

    std::vector<std::byte> movedFirst = good;
    patchUnsigned(movedFirst, indexOffset + 4u, 64u, 8u);
    EXPECT_EQ(an::readGaiaChunkLayout(execution, movedFirst, metadata, metadata.chunks.front()).status().code(),
              an::ErrorCode::InvalidData);

    std::vector<std::byte> backwards = good;
    patchUnsigned(backwards, indexOffset + 24u + 4u, 64u, 8u);
    EXPECT_EQ(an::readGaiaChunkLayout(execution, backwards, metadata, metadata.chunks.front()).status().code(),
              an::ErrorCode::InvalidData);

    std::vector<std::byte> pastEnd = good;
    patchUnsigned(pastEnd, indexOffset + 24u + 4u, 192u, 8u);
    EXPECT_EQ(an::readGaiaChunkLayout(execution, pastEnd, metadata, metadata.chunks.front()).status().code(),
              an::ErrorCode::InvalidData);

    std::vector<std::byte> truncatedIndex = good;
    truncatedIndex.resize(indexOffset + 12u);
    EXPECT_EQ(an::readGaiaChunkLayout(execution, truncatedIndex, metadata, metadata.chunks.front()).status().code(),
              an::ErrorCode::InvalidData);
}

TEST(GaiaChunkReaderTest, ReadingStopsWhenTheCallerAsksItTo)
{
    const an::EngineContext engine = makeEngine();
    std::stop_source stop;
    const an::ExecutionContext execution = makeExecution(engine, 4u, stop.get_token());
    const std::vector<FixtureTile> tiles = threeTiles();
    const std::vector<std::byte> bytes = makeChunkFile(tiles);
    const an::GaiaCatalogMetadata metadata = makeMetadata(3u, 3);

    stop.request_stop();
    EXPECT_EQ(an::readGaiaChunkLayout(execution, bytes, metadata, metadata.chunks.front()).status().code(),
              an::ErrorCode::Cancelled);
}

} // namespace

TEST(GaiaChunkReaderTest, TileSpansCannotOverlapOrStartInsideARecord)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    const auto metadata = makeMetadata(3u, 3);
    for (const std::uint64_t offset : {0ull, 64ull, 129ull})
    {
        auto bytes = makeChunkFile(threeTiles());
        patchUnsigned(bytes, chunkIndexOffset(bytes) + 2u * an::kGaiaChunkIndexEntryBytes + 4u, offset, 8u);
        const auto refused = an::readGaiaChunkLayout(execution, bytes, metadata, metadata.chunks[0]);
        EXPECT_EQ(refused.code(), an::ErrorCode::InvalidData);
    }
    const auto bytes = makeChunkFile(threeTiles());
    ASSERT_TRUE(an::readGaiaChunkLayout(execution, bytes, metadata, metadata.chunks[0]).hasValue());
}

TEST(GaiaChunkReaderTest, OptionalHeaderCountDoesNotReplaceTheManifestCount)
{
    const auto engine = makeEngine();
    const auto execution = makeExecution(engine);
    ChunkFixtureOptions options;
    auto header = gaia_test::makeChunkHeaderText(options, 3u, 3u);
    const auto field = header.find(",\"total_stars_in_chunk\"");
    ASSERT_NE(field, std::string::npos);
    header.erase(field);
    header += "}";
    options.headerTextOverride = header;
    const auto bytes = makeChunkFile(threeTiles(), options);
    auto metadata = makeMetadata(3u, 3);
    EXPECT_TRUE(an::readGaiaChunkLayout(execution, bytes, metadata, metadata.chunks.front()).hasValue());
    metadata.chunks.front().recordCount = 4u;
    EXPECT_EQ(an::readGaiaChunkLayout(execution, bytes, metadata, metadata.chunks.front()).code(),
              an::ErrorCode::InvalidData);
}
