// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a Gaia catalog says about itself, and the tile map it implies.
///
/// A Gaia catalog is a manifest beside its records. The records are stored either as one file per
/// sky tile or as a few chunk files each holding a contiguous run of tiles behind its own index
/// (`GaiaChunkReader.h`). `parseGaiaCatalogManifest` reads the manifest text into a
/// `GaiaCatalogMetadata` value and refuses the combinations that cannot describe one catalog;
/// `buildGaiaTileMap` proves that a chunked catalog's chunk list covers the sky exactly once and
/// says which chunk owns each tile; `makeGaiaTilePath` names a tile file of a tile-stored catalog.
///
/// Nothing here opens a file. Paths are described relative to the catalog root and checked to stay
/// inside it, and a stated checksum is carried but never verified. Turning a direction into a tile
/// index is `Healpix.h`'s job, so the tile count arrives as a number.

#pragma once

#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogRecord.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace an
{

/// One operation's policy and engine-context lease. Defined in `Runtime/ExecutionContext.h`.
class ExecutionContext;

/// The most bytes of manifest text one parse reads.
///
/// A manifest describes a catalog rather than holding one: the shipping table's is a few kilobytes
/// of chunk descriptions, and a megabyte of it is a file that is not a manifest.
inline constexpr std::uint32_t kDefaultMaxGaiaManifestBytes = 1u * 1024u * 1024u;

/// The most chunk descriptions one manifest may list.
inline constexpr std::uint32_t kDefaultMaxGaiaChunkCount = 65536u;

/// How the records of a Gaia catalog are laid out across files.
enum class GaiaCatalogStorage : std::uint8_t
{
    /// One file per tile, named after its tile index, holding nothing but records.
    Tiles = 0u,

    /// A few large files, each holding a contiguous run of tiles behind its own index.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Chunks = 1u
};

/// Whether a value names a storage arrangement this build reads.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param storage the value to inspect.
/// @return true for one of the two declared arrangements; false for an out-of-range cast.
[[nodiscard]] constexpr bool isGaiaCatalogStorage(GaiaCatalogStorage storage) noexcept
{
    return storage == GaiaCatalogStorage::Tiles || storage == GaiaCatalogStorage::Chunks;
}

/// The arrangement's own name, for a log line, a diagnostic field or a failure message.
///
/// Thread-safe, reentrant, and allocation-free.
///
/// @param storage the arrangement to name.
/// @return a null-terminated string literal compiled into this library, which outlives every
///         caller; `"UnknownGaiaCatalogStorage"` for an out-of-range cast.
[[nodiscard]] AFTERNIGHTENGINE_API const char* gaiaCatalogStorageName(GaiaCatalogStorage storage) noexcept;

/// One chunk file, as the manifest describes it.
struct GaiaChunkDescriptor
{
    /// The chunk's position in the manifest's list, which `buildGaiaTileMap` requires to equal its
    /// index in that list.
    std::int32_t index = 0;

    /// The chunk file's path, relative to the catalog root, as the manifest spells it.
    ///
    /// The manifest parse carries it unchecked; `buildGaiaTileMap` refuses a path that is empty,
    /// absolute, drive-qualified or contains `..` anywhere, and two chunks naming the same path.
    std::string file{};

    /// The first sky tile this chunk holds.
    std::int32_t firstTile = 0;

    /// How many consecutive sky tiles this chunk holds.
    std::int32_t tileCount = 0;

    /// How many records the chunk holds across all of its tiles.
    std::uint64_t recordCount = 0u;

    /// How many bytes the chunk file holds, or zero when the manifest states none.
    std::uint64_t sizeBytes = 0u;

    /// The chunk file's stated content digest, or empty when the manifest states none.
    ///
    /// Carried and reported, never verified here: computing it means reading the file, and this
    /// engine reads none.
    std::string checksum{};

    /// Whether two descriptions are the same.
    ///
    /// @param left one description.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend bool operator==(const GaiaChunkDescriptor& left, const GaiaChunkDescriptor& right) = default;
};

/// What a Gaia catalog states about itself.
///
/// A value with no shared state behind it, so a catalog that failed to load is never mistaken for a
/// shared default.
struct GaiaCatalogMetadata
{
    /// The manifest format's own version number.
    std::int32_t formatVersion = 1;

    /// How the records are laid out across files.
    GaiaCatalogStorage storage = GaiaCatalogStorage::Tiles;

    /// How many bytes one record occupies.
    ///
    /// When the manifest states none: 64 for chunk storage, and for tile storage 44 at format
    /// version 2 or later and 36 before it.
    std::uint32_t recordSizeBytes = 0u;

    /// Which fixed-size layout `recordSizeBytes` names.
    GaiaRecordLayout recordLayout = GaiaRecordLayout::Version1;

    /// The order of the sky pixelisation the tiles are indexed by.
    std::int32_t pixelisationOrder = 5;

    /// How the pixelisation numbers its tiles, in the pixelisation's own vocabulary.
    ///
    /// Carried as text and compared rather than interpreted: this surface reads a provider's tile
    /// numbering, and turning a direction into a tile index belongs to the projection surface. The
    /// parse reads only `"ring"` and refuses anything else as `ErrorCode::Unsupported`.
    std::string pixelOrdering = "ring";

    /// The catalog's display name.
    std::string catalogName = "Gaia DR3 Local Catalog";

    /// The catalog's stable identifier, which every chunk header must repeat.
    std::string catalogIdentifier = "gaia_dr3";

    /// The epoch the positions are stated at, in Julian years.
    double epochYear = kGaiaDefaultEpochYear;

    /// The faintest G magnitude the catalog was built to hold.
    double magnitudeLimit = 14.0;

    /// How many records the catalog holds, or zero when the manifest states none.
    std::uint64_t totalRecordCount = 0u;

    /// The directory tile files live in, relative to the catalog root. Empty for chunk storage.
    std::string tileDirectory{};

    /// The chunk files, in manifest order. Empty for tile storage.
    std::vector<GaiaChunkDescriptor> chunks{};

    /// Whether two descriptions are the same.
    ///
    /// @param left one description.
    /// @param right the other.
    /// @return true when every member is equal.
    [[nodiscard]] friend bool operator==(const GaiaCatalogMetadata& left, const GaiaCatalogMetadata& right) = default;
};

/// How much manifest text one parse is allowed to read.
struct GaiaManifestLimits
{
    /// The most bytes of manifest text this parse reads.
    std::uint32_t maximumBytes = kDefaultMaxGaiaManifestBytes;

    /// The most chunk descriptions this parse produces.
    std::uint32_t maximumChunks = kDefaultMaxGaiaChunkCount;
};

/// Everything one successful manifest parse produced.
struct ParsedGaiaManifest
{
    /// What the catalog states about itself.
    GaiaCatalogMetadata metadata{};

    /// Whether the manifest stated a record count of its own.
    ///
    /// The difference between a manifest that promises a total - which a loader then holds the
    /// chunk contents to - and one that promises nothing, whose total is whatever the chunks turn
    /// out to hold.
    bool statedTotalRecordCount = false;
};

/// Reads a Gaia catalog manifest.
///
/// The text is the manifest object this engine's Gaia tables ship with. Every member is optional
/// and falls back to the value the format defaults it to, and the whole object is then checked for
/// the combinations that cannot describe one catalog: an unsupported storage name or tile
/// numbering, a record size no layout has, a tile-stored catalog in the 64-byte layout or with a
/// tile directory that is empty or reaches outside the catalog, a chunked catalog that is not the
/// format version and record size chunks are defined for, and a stated chunk count the chunk list
/// disagrees with. Chunk descriptions are read as written; `buildGaiaTileMap` is what checks them
/// against the sky.
///
/// Parsing is a pure function of bytes. This engine opens no files, so a caller supplies the
/// manifest text the same way it supplies a catalog text, and what the manifest names is described
/// rather than opened.
///
/// Runs on the calling thread, checks cancellation before it begins and at bounded chunk
/// boundaries, and allocates the description it returns and a transient list of scanned members.
///
/// @param execution the operation policy and engine-context lease.
/// @param manifestText the whole manifest, which is only read and need not outlive the call.
/// @param limits how much text the parse may read and how many chunks it may produce.
/// @return the description; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::InvalidArgument` for a zero limit; `ErrorCode::LimitExceeded` when the text
///         exceeds its byte limit, carrying the requested and permitted byte counts, or when the
///         chunk list or one object holds more entries than admitted, which carries no counts;
///         `ErrorCode::Unsupported` for a storage arrangement, tile numbering or record size this
///         build does not read, or a tile-stored catalog in the 64-byte layout;
///         `ErrorCode::InvalidData` for text that is not one object or nests too deeply, a member of
///         the wrong kind, or a manifest that contradicts itself; or cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<ParsedGaiaManifest>
parseGaiaCatalogManifest(const ExecutionContext& execution, std::string_view manifestText,
                         const GaiaManifestLimits& limits = {});

/// What building a tile map is asked to cover.
struct GaiaTileMapRequest
{
    /// How many tiles the manifest's pixelisation order covers.
    ///
    /// Supplied by the caller rather than derived here: turning a pixelisation order into a tile
    /// count, and a direction into a tile index, is what the projection surface is for, and this
    /// surface reads a provider's own numbering rather than owning it.
    std::uint32_t skyTileCount = 0u;
};

/// Which chunk owns each sky tile.
struct GaiaTileMap
{
    /// One entry per sky tile, holding the position in `GaiaCatalogMetadata::chunks` that owns it.
    ///
    /// Every entry is a valid position: a map is only built when the chunk ranges cover the whole
    /// sky exactly once.
    std::vector<std::uint32_t> tileChunkIndices{};

    /// How many records the chunk descriptions add up to.
    std::uint64_t totalRecordCount = 0u;
};

/// Builds the tile-to-chunk map a chunked catalog implies, and checks that it can exist.
///
/// The manifest's chunk list is an index over the sky, and this is where it is proved to be one:
/// positions contiguous from zero, every path relative, contained and distinct, every chunk holding
/// at least one tile, every tile range inside the sky and disjoint from the others, and every tile
/// covered. A manifest that fails any of those describes a catalog with a hole or an overlap in it,
/// which is a broken installation rather than a catalog to answer queries from.
///
/// Runs on the calling thread, checks cancellation at bounded chunk boundaries, and allocates the
/// map it returns and a transient list of the paths already seen. The distinct-path check compares
/// each path with every earlier one.
///
/// @param execution the operation policy and engine-context lease.
/// @param metadata the catalog description to read the chunk list from.
/// @param request how many tiles the pixelisation covers.
/// @return the map; `ErrorCode::NotInitialized` when `execution` holds nothing;
///         `ErrorCode::Unsupported` for a catalog that is not chunk-stored;
///         `ErrorCode::InvalidArgument` for a zero tile count; `ErrorCode::InvalidData` for an empty
///         chunk list or one that is not an index over the sky, naming which rule it failed; or
///         cancellation.
[[nodiscard]] AFTERNIGHTENGINE_API Result<GaiaTileMap> buildGaiaTileMap(const ExecutionContext& execution,
                                                                        const GaiaCatalogMetadata& metadata,
                                                                        const GaiaTileMapRequest& request);

/// The path a tile file of a tile-stored catalog is expected at, relative to the catalog root.
///
/// The provider's own file-naming rule, which a caller reads bytes with. Nothing here opens it.
///
/// Thread-safe and reentrant. It allocates only the returned string.
///
/// @param metadata the catalog description to read the tile directory from.
/// @param tileIndex which tile to name. A negative index names nothing and returns an empty path.
/// @return the relative path, such as `"healpix_order5/tile_01234.bin"`.
[[nodiscard]] AFTERNIGHTENGINE_API std::string makeGaiaTilePath(const GaiaCatalogMetadata& metadata,
                                                                std::int32_t tileIndex);

} // namespace an
