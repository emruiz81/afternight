// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The container a chunked Gaia catalog stores its tiles in.
///
/// A chunk file is, in order: a sixteen-byte little-endian preamble - the magic `ANSP`, the
/// container version, a flags field and a reserved field that must both be zero, and the length of
/// the header text; a JSON header that repeats the catalog identity and the chunk's place in the
/// manifest; a tile index of twelve-byte entries - a record count and an offset into the record
/// area - starting at the first eight-byte boundary after the header; and the record area, which
/// holds every tile's records in the 64-byte layout.
///
/// `readGaiaChunkLayout` checks all of that against the manifest that named the chunk and against
/// the file's own length once, so `gaiaChunkTileRecordBytes` can reach a tile's records by
/// arithmetic and hand them to `decodeGaiaRecords` without copying. Nothing here opens or maps a
/// file.

#pragma once

#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogManifest.h>
#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogRecord.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The four bytes a chunk file begins with.
inline constexpr const char* kGaiaChunkMagic = "ANSP";

/// The container version this build reads.
inline constexpr std::uint16_t kGaiaChunkContainerVersion = 1u;

/// How many bytes the fixed part of a chunk file's preamble occupies.
inline constexpr std::uint32_t kGaiaChunkPreambleBytes = 16u;

/// How many bytes one entry of a chunk file's tile index occupies.
inline constexpr std::uint32_t kGaiaChunkIndexEntryBytes = 12u;

/// The most bytes of a chunk file's own header text this build reads.
inline constexpr std::uint32_t kDefaultMaxGaiaChunkHeaderBytes = 1u * 1024u * 1024u;

/// Where one tile's records sit inside a chunk file.
struct GaiaChunkTileSpan
{
    /// How many records the tile holds.
    std::uint32_t recordCount = 0u;

    /// Where the tile's first record begins, in bytes from the start of the record area.
    std::uint64_t recordAreaOffset = 0u;

    /// Whether two spans are the same two numbers.
    ///
    /// @param left one span.
    /// @param right the other.
    /// @return true when both members are equal.
    [[nodiscard]] friend constexpr bool operator==(const GaiaChunkTileSpan& left,
                                                   const GaiaChunkTileSpan& right) noexcept = default;
};

/// How much of a chunk file one read is allowed to look at.
struct GaiaChunkLimits
{
    /// The most bytes of the chunk's own header text this read parses.
    std::uint32_t maximumHeaderBytes = kDefaultMaxGaiaChunkHeaderBytes;

    /// The most tiles one chunk may hold.
    std::uint32_t maximumTiles = kDefaultMaxGaiaChunkCount;
};

/// What one chunk file holds and where.
///
/// The chunk's own index, checked against the manifest that named it. Every offset in it has
/// already been proved to lie inside the file, which is what lets a tile be reached by arithmetic
/// afterwards rather than by a second round of validation.
struct GaiaChunkLayout
{
    /// The chunk's position in the manifest's list.
    std::int32_t chunkIndex = 0;

    /// The first sky tile this chunk holds.
    std::int32_t firstTile = 0;

    /// Which fixed-size layout the chunk's records are in: always `GaiaRecordLayout::Version3`, the
    /// only layout chunks are written with.
    GaiaRecordLayout recordLayout = GaiaRecordLayout::Version3;

    /// Where the record area begins, in bytes from the start of the file.
    std::uint64_t recordAreaOffset = 0u;

    /// How many records the chunk holds across all of its tiles.
    std::uint64_t totalRecordCount = 0u;

    /// One span per tile the chunk holds, in ascending tile order.
    std::vector<GaiaChunkTileSpan> tiles{};

    /// Whether two layouts describe the same chunk.
    ///
    /// @param left one layout.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend bool operator==(const GaiaChunkLayout& left, const GaiaChunkLayout& right) = default;
};

/// Reads a chunk file's preamble, header and tile index.
///
/// Everything the container states about itself is checked against the manifest that named it -
/// the catalog identity, the tile numbering, the record layout, the chunk's position and tile range,
/// the number of chunks, the file size when the manifest states one, and the record total - and
/// everything it states about its own bytes is checked against how many there are: the index
/// begins at the first eight-byte boundary after the header, the first tile begins the record
/// area, offsets never go backwards and stay on whole records, and no tile's records end past the
/// file.
///
/// The record total must agree three ways - the tiles' sum, the header's `total_stars_in_chunk`
/// and the manifest's count - and a header that omits that member states a total of zero.
///
/// The whole chunk file is passed as bytes because this engine opens no files. A caller that has
/// mapped one hands the mapping in; nothing here retains it.
///
/// Runs on the calling thread, checks cancellation at bounded tile boundaries, and allocates the
/// layout it returns and the transiently scanned header members.
///
/// @param execution the operation policy and engine-context lease.
/// @param chunkBytes the whole chunk file, which is only read and need not outlive the call.
/// @param metadata the catalog description the chunk must agree with.
/// @param descriptor the manifest entry that named this chunk.
/// @param limits how much header text and how many tiles the read admits.
/// @return the layout; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Unsupported` for a catalog that is not chunk-stored, a container version this
///         build does not read, or a non-zero flags or reserved field; `ErrorCode::InvalidArgument`
///         for a zero limit or a descriptor that is not one of the manifest's chunks;
///         `ErrorCode::InvalidData` for a chunk whose bytes, header or index disagree with the
///         manifest or with themselves, naming which and carrying the chunk's path;
///         `ErrorCode::LimitExceeded` when the header or tile count exceeds its limit, carrying the
///         requested and permitted values; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GaiaChunkLayout> readGaiaChunkLayout(const ExecutionContext& execution,
                                                                               std::span<const std::byte> chunkBytes,
                                                                               const GaiaCatalogMetadata& metadata,
                                                                               const GaiaChunkDescriptor& descriptor,
                                                                               const GaiaChunkLimits& limits = {});

/// The bytes one tile's records occupy inside a chunk file.
///
/// A view into the caller's own bytes rather than a copy: the records are decoded from it by
/// `decodeGaiaRecords`, and a caller that wants only their count reads the layout instead.
///
/// Thread-safe with respect to `chunkBytes` and `layout`, which are only read. Allocation-free on
/// success.
///
/// @param chunkBytes the same whole chunk file the layout was read from.
/// @param layout the layout read from those bytes.
/// @param localTileIndex the tile's position within this chunk, which is its sky tile index less
///        `GaiaChunkLayout::firstTile`.
/// @return a view into `chunkBytes`, which must outlive it; `ErrorCode::OutOfRange` when the chunk
///         holds no such tile; `ErrorCode::InvalidData` when the layout does not describe the bytes
///         it is given, which is what a layout read from a different file looks like. A tile with
///         no records is a success with an empty view.
[[nodiscard]] AFTERNIGHTENGINE_API Result<std::span<const std::byte>>
gaiaChunkTileRecordBytes(std::span<const std::byte> chunkBytes, const GaiaChunkLayout& layout,
                         std::int32_t localTileIndex);

} // namespace an
