// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - reading a Gaia catalog's own description and the tile index it implies.

#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogManifest.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/catalog/CatalogInternal.h"
#include "algorithms/catalog/CatalogJson.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace an
{
namespace
{

using catalog_detail::catalogFailure;
using catalog_detail::catalogLimitFailure;
using catalog_detail::findJsonMember;
using catalog_detail::jsonInteger;
using catalog_detail::jsonIsArray;
using catalog_detail::JsonLimits;
using catalog_detail::JsonMember;
using catalog_detail::jsonNumber;
using catalog_detail::JsonScanStatus;
using catalog_detail::jsonString;
using catalog_detail::jsonUnsigned;
using catalog_detail::scanJsonArray;
using catalog_detail::scanJsonObject;

constexpr const char* kManifestOperation = "an::parseGaiaCatalogManifest";
constexpr const char* kTileMapOperation = "an::buildGaiaTileMap";
constexpr std::uint32_t kCancellationStride = 1024u;

/// The storage names the manifest format spells.
constexpr std::string_view kTilesStorageName = "tiles";
constexpr std::string_view kChunksStorageName = "chunks";

/// The tile numbering this build reads.
constexpr std::string_view kSupportedPixelOrdering = "ring";

/// The manifest format version a chunked catalog is defined at.
constexpr std::int32_t kChunkedFormatVersion = 3;

/// How many characters a tile file's index is padded to in its name.
constexpr std::size_t kTileNameDigits = 5u;

/// The most members one object of these documents may hold.
///
/// A property of the format rather than of the caller: the descriptions here carry ten members and a
/// chunk seven, and an object with thousands of them is not one of them.
constexpr std::uint32_t kMaxObjectMembers = 4096u;

/// How deeply one of these documents may nest.
constexpr std::uint32_t kMaxDocumentDepth = 16u;

/// Turns a scan refusal into the operation's own failure.
///
/// @param status why the scan stopped.
/// @param what the document that would not scan, for the message.
/// @param operation the engine operation that failed.
/// @return the failure.
[[nodiscard]] Status scanFailure(const JsonScanStatus status, const char* what, const char* operation)
{
    switch (status)
    {
    case JsonScanStatus::TooManyEntries:
        return catalogFailure(ErrorCode::LimitExceeded, what, operation);
    case JsonScanStatus::TooDeep:
        return catalogFailure(ErrorCode::InvalidData, what, operation);
    case JsonScanStatus::Malformed:
    case JsonScanStatus::Ok:
        break;
    }
    return catalogFailure(ErrorCode::InvalidData, what, operation);
}

/// Reads an optional text member.
///
/// @param members the object's members.
/// @param name the member to read.
/// @param fallback what an absent member means.
/// @param value receives the text.
/// @return true when the member was absent or was well-formed text.
[[nodiscard]] bool readText(const std::span<const JsonMember> members, const std::string_view name,
                            std::string fallback, std::string& value)
{
    const JsonMember* const member = findJsonMember(members, name);
    if (member == nullptr)
    {
        value = std::move(fallback);
        return true;
    }
    std::optional<std::string> parsed = jsonString(member->value);
    if (!parsed.has_value())
    {
        return false;
    }
    value = std::move(*parsed);
    return true;
}

/// Reads an optional whole-number member.
///
/// @param members the object's members.
/// @param name the member to read.
/// @param fallback what an absent member means.
/// @param value receives the number.
/// @return true when the member was absent or was a whole number in range.
[[nodiscard]] bool readInteger(const std::span<const JsonMember> members, const std::string_view name,
                               const std::int64_t fallback, std::int64_t& value)
{
    const JsonMember* const member = findJsonMember(members, name);
    if (member == nullptr)
    {
        value = fallback;
        return true;
    }
    const std::optional<std::int64_t> parsed = jsonInteger(member->value);
    if (!parsed.has_value())
    {
        return false;
    }
    value = *parsed;
    return true;
}

/// Reads an optional non-negative whole-number member.
///
/// @param members the object's members.
/// @param name the member to read.
/// @param fallback what an absent member means.
/// @param value receives the number.
/// @return true when the member was absent or was a non-negative whole number in range.
[[nodiscard]] bool readUnsigned(const std::span<const JsonMember> members, const std::string_view name,
                                const std::uint64_t fallback, std::uint64_t& value)
{
    const JsonMember* const member = findJsonMember(members, name);
    if (member == nullptr)
    {
        value = fallback;
        return true;
    }
    const std::optional<std::uint64_t> parsed = jsonUnsigned(member->value);
    if (!parsed.has_value())
    {
        return false;
    }
    value = *parsed;
    return true;
}

/// Reads an optional number member.
///
/// @param members the object's members.
/// @param name the member to read.
/// @param fallback what an absent member means.
/// @param value receives the number.
/// @return true when the member was absent or was a finite number.
[[nodiscard]] bool readNumber(const std::span<const JsonMember> members, const std::string_view name,
                              const double fallback, double& value)
{
    const JsonMember* const member = findJsonMember(members, name);
    if (member == nullptr)
    {
        value = fallback;
        return true;
    }
    const std::optional<double> parsed = jsonNumber(member->value);
    if (!parsed.has_value())
    {
        return false;
    }
    value = *parsed;
    return true;
}

/// Whether a manifest-relative path is one this engine will let a caller resolve.
///
/// A chunk path names a file beside the catalog. One that is absolute, or that steps out of the
/// catalog directory, describes a file somewhere else entirely, and a manifest is not a place to
/// accept that from.
///
/// @param path the path the manifest stated.
/// @return true for a non-empty path that is neither rooted nor drive-qualified and contains no
///         parent-directory component. Adjacent dots inside a file name are ordinary characters.
[[nodiscard]] bool isContainedRelativePath(const std::string_view path) noexcept
{
    if (path.empty())
    {
        return false;
    }
    if (path.front() == '/' || path.front() == '\\')
    {
        return false;
    }
    if (path.size() >= 2u && path[1] == ':')
    {
        return false;
    }
    std::size_t begin = 0u;
    while (begin <= path.size())
    {
        std::size_t end = begin;
        while (end < path.size() && path[end] != '/' && path[end] != '\\')
        {
            ++end;
        }
        if (path.substr(begin, end - begin) == "..")
        {
            return false;
        }
        begin = end + 1u;
    }
    return true;
}

} // namespace

const char* gaiaCatalogStorageName(const GaiaCatalogStorage storage) noexcept
{
    switch (storage)
    {
    case GaiaCatalogStorage::Tiles:
        return "Tiles";
    case GaiaCatalogStorage::Chunks:
        return "Chunks";
    }
    return "UnknownGaiaCatalogStorage";
}

Result<ParsedGaiaManifest> parseGaiaCatalogManifest(const ExecutionContext& execution,
                                                    const std::string_view manifestText,
                                                    const GaiaManifestLimits& limits)
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "the operation has no engine context", kManifestOperation);
    }
    if (limits.maximumBytes == 0u || limits.maximumChunks == 0u)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "a manifest limit admits nothing", kManifestOperation);
    }
    if (manifestText.size() > limits.maximumBytes)
    {
        return catalogLimitFailure("the manifest text is larger than the limit admits", kManifestOperation,
                                   manifestText.size(), limits.maximumBytes);
    }
    if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
    {
        return cancelled;
    }

    // Two ceilings, because they bound two different things: how many members one object may hold,
    // which is a property of the format, and how many chunks the caller will accept, which is not.
    const JsonLimits objectLimits{.maximumEntries = kMaxObjectMembers, .maximumDepth = kMaxDocumentDepth};
    const JsonLimits chunkListLimits{.maximumEntries = limits.maximumChunks, .maximumDepth = kMaxDocumentDepth};
    std::vector<JsonMember> members;
    if (const JsonScanStatus status = scanJsonObject(manifestText, objectLimits, members); status != JsonScanStatus::Ok)
    {
        return scanFailure(status, "the manifest text is not one object of members", kManifestOperation);
    }

    ParsedGaiaManifest parsed;
    GaiaCatalogMetadata& metadata = parsed.metadata;

    std::int64_t formatVersion = 1;
    std::int64_t pixelisationOrder = 5;
    std::string storageName;
    if (!readInteger(members, "format_version", 1, formatVersion) ||
        !readInteger(members, "healpix_order", 5, pixelisationOrder) ||
        !readText(members, "storage", std::string(kTilesStorageName), storageName) ||
        !readText(members, "pixel_ordering", std::string(kSupportedPixelOrdering), metadata.pixelOrdering) ||
        !readText(members, "catalog_name", "Gaia DR3 Local Catalog", metadata.catalogName) ||
        !readText(members, "catalog_id", "gaia_dr3", metadata.catalogIdentifier) ||
        !readNumber(members, "epoch_year", kGaiaDefaultEpochYear, metadata.epochYear) ||
        !readNumber(members, "magnitude_limit", 14.0, metadata.magnitudeLimit))
    {
        return catalogFailure(ErrorCode::InvalidData, "a manifest member is not the kind of value it must be",
                              kManifestOperation);
    }
    if (formatVersion < 0 || formatVersion > 0x7FFFFFFF || pixelisationOrder < 0 || pixelisationOrder > 0x7FFFFFFF)
    {
        return catalogFailure(ErrorCode::InvalidData,
                              "the manifest states a format version or tile order that is not a"
                              " count",
                              kManifestOperation);
    }
    metadata.formatVersion = static_cast<std::int32_t>(formatVersion);
    metadata.pixelisationOrder = static_cast<std::int32_t>(pixelisationOrder);

    if (storageName == kTilesStorageName)
    {
        metadata.storage = GaiaCatalogStorage::Tiles;
    }
    else if (storageName == kChunksStorageName)
    {
        metadata.storage = GaiaCatalogStorage::Chunks;
    }
    else
    {
        // Reading anything that is not the chunk name as tile storage would turn a manifest with a
        // spelling mistake in it into a tile catalog with no tiles.
        return catalogFailure(ErrorCode::Unsupported,
                              "the manifest states a storage arrangement this build does not"
                              " read",
                              kManifestOperation);
    }

    if (metadata.pixelOrdering != kSupportedPixelOrdering)
    {
        return catalogFailure(ErrorCode::Unsupported, "the manifest states a tile numbering this build does not read",
                              kManifestOperation);
    }

    parsed.statedTotalRecordCount = findJsonMember(members, "total_stars") != nullptr;
    if (!readUnsigned(members, "total_stars", 0u, metadata.totalRecordCount))
    {
        return catalogFailure(ErrorCode::InvalidData, "the manifest states a record total that is not a count",
                              kManifestOperation);
    }

    const std::uint64_t defaultRecordSize =
        metadata.storage == GaiaCatalogStorage::Chunks
            ? gaiaRecordSizeBytes(GaiaRecordLayout::Version3)
            : gaiaRecordSizeBytes(metadata.formatVersion >= 2 ? GaiaRecordLayout::Version2
                                                              : GaiaRecordLayout::Version1);
    std::uint64_t recordSizeBytes = 0u;
    if (!readUnsigned(members, "record_size_bytes", defaultRecordSize, recordSizeBytes))
    {
        return catalogFailure(ErrorCode::InvalidData, "the manifest states a record size that is not a count",
                              kManifestOperation);
    }
    const std::optional<GaiaRecordLayout> layout = gaiaRecordLayoutForSize(recordSizeBytes);
    if (!layout.has_value())
    {
        return catalogFailure(ErrorCode::Unsupported, "the manifest states a record size no layout has",
                              kManifestOperation);
    }
    metadata.recordSizeBytes = static_cast<std::uint32_t>(recordSizeBytes);
    metadata.recordLayout = *layout;

    if (metadata.storage == GaiaCatalogStorage::Tiles)
    {
        if (metadata.recordLayout == GaiaRecordLayout::Version3)
        {
            return catalogFailure(ErrorCode::Unsupported,
                                  "a tile-stored catalog is not written with this record"
                                  " layout",
                                  kManifestOperation);
        }
        if (!readText(members, "tile_directory", "healpix_order" + std::to_string(metadata.pixelisationOrder),
                      metadata.tileDirectory))
        {
            return catalogFailure(ErrorCode::InvalidData, "the manifest states a tile directory that is not text",
                                  kManifestOperation);
        }
        if (!isContainedRelativePath(metadata.tileDirectory))
        {
            return catalogFailure(ErrorCode::InvalidData, "the manifest states a tile directory outside the catalog",
                                  kManifestOperation);
        }
        return parsed;
    }

    if (metadata.formatVersion != kChunkedFormatVersion)
    {
        return catalogFailure(ErrorCode::InvalidData,
                              "a chunk-stored catalog states a format version chunks are not"
                              " defined at",
                              kManifestOperation);
    }
    if (metadata.recordLayout != GaiaRecordLayout::Version3)
    {
        return catalogFailure(ErrorCode::InvalidData,
                              "a chunk-stored catalog states a record size chunks are not"
                              " written with",
                              kManifestOperation);
    }

    std::int64_t statedChunkCount = 0;
    if (!readInteger(members, "chunk_count", 0, statedChunkCount) || statedChunkCount < 0)
    {
        return catalogFailure(ErrorCode::InvalidData, "the manifest states a chunk count that is not a count",
                              kManifestOperation);
    }

    const JsonMember* const chunksMember = findJsonMember(members, "chunks");
    if (chunksMember != nullptr)
    {
        if (!jsonIsArray(chunksMember->value))
        {
            return catalogFailure(ErrorCode::InvalidData, "the manifest's chunk list is not an array",
                                  kManifestOperation);
        }
        std::vector<std::string_view> elements;
        if (const JsonScanStatus status = scanJsonArray(chunksMember->value, chunkListLimits, elements);
            status != JsonScanStatus::Ok)
        {
            return scanFailure(status, "the manifest's chunk list is not a list of chunks", kManifestOperation);
        }

        metadata.chunks.reserve(elements.size());
        std::vector<JsonMember> chunkMembers;
        for (std::size_t position = 0u; position < elements.size(); ++position)
        {
            if ((position % kCancellationStride) == 0u)
            {
                if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
                {
                    return cancelled;
                }
            }
            if (const JsonScanStatus status = scanJsonObject(elements[position], objectLimits, chunkMembers);
                status != JsonScanStatus::Ok)
            {
                return scanFailure(status, "a chunk description is not one object of members", kManifestOperation);
            }

            GaiaChunkDescriptor chunk;
            std::int64_t index = 0;
            std::int64_t firstTile = 0;
            std::int64_t tileCount = 0;
            if (!readInteger(chunkMembers, "index", 0, index) ||
                !readInteger(chunkMembers, "first_tile", 0, firstTile) ||
                !readInteger(chunkMembers, "tile_count", 0, tileCount) ||
                !readUnsigned(chunkMembers, "stars", 0u, chunk.recordCount) ||
                !readUnsigned(chunkMembers, "size_bytes", 0u, chunk.sizeBytes) ||
                !readText(chunkMembers, "file", std::string{}, chunk.file) ||
                !readText(chunkMembers, "sha256", std::string{}, chunk.checksum))
            {
                return catalogFailure(ErrorCode::InvalidData,
                                      "a chunk description holds a member that is not the kind"
                                      " of value it must be",
                                      kManifestOperation);
            }
            if (index < 0 || index > 0x7FFFFFFF || firstTile < 0 || firstTile > 0x7FFFFFFF || tileCount < 0 ||
                tileCount > 0x7FFFFFFF)
            {
                return catalogFailure(ErrorCode::InvalidData,
                                      "a chunk description states a position or tile range that"
                                      " is not a count",
                                      kManifestOperation);
            }
            chunk.index = static_cast<std::int32_t>(index);
            chunk.firstTile = static_cast<std::int32_t>(firstTile);
            chunk.tileCount = static_cast<std::int32_t>(tileCount);
            metadata.chunks.push_back(std::move(chunk));
        }
    }

    if (statedChunkCount != 0 && static_cast<std::size_t>(statedChunkCount) != metadata.chunks.size())
    {
        return catalogFailure(ErrorCode::InvalidData, "the manifest's chunk count and chunk list disagree",
                              kManifestOperation);
    }
    return parsed;
}

Result<GaiaTileMap> buildGaiaTileMap(const ExecutionContext& execution, const GaiaCatalogMetadata& metadata,
                                     const GaiaTileMapRequest& request)
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "the operation has no engine context", kTileMapOperation);
    }
    if (metadata.storage != GaiaCatalogStorage::Chunks)
    {
        return catalogFailure(ErrorCode::Unsupported, "only a chunk-stored catalog has a tile-to-chunk map",
                              kTileMapOperation);
    }
    if (request.skyTileCount == 0u)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the pixelisation covers no tiles", kTileMapOperation);
    }
    if (metadata.chunks.empty())
    {
        return catalogFailure(ErrorCode::InvalidData, "the manifest lists no chunks to cover the sky with",
                              kTileMapOperation);
    }

    GaiaTileMap map;
    map.tileChunkIndices.assign(request.skyTileCount, std::numeric_limits<std::uint32_t>::max());

    std::vector<std::string_view> seenPaths;
    seenPaths.reserve(metadata.chunks.size());
    for (std::size_t position = 0u; position < metadata.chunks.size(); ++position)
    {
        if ((position % kCancellationStride) == 0u)
        {
            if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
            {
                return cancelled;
            }
        }

        const GaiaChunkDescriptor& chunk = metadata.chunks[position];
        if (chunk.index != static_cast<std::int32_t>(position))
        {
            return catalogFailure(ErrorCode::InvalidData, "the chunk positions are not contiguous from zero",
                                  kTileMapOperation);
        }
        if (!isContainedRelativePath(chunk.file))
        {
            return catalogFailure(ErrorCode::InvalidData, "a chunk names no file, or one outside the catalog",
                                  kTileMapOperation);
        }
        for (const std::string_view seen : seenPaths)
        {
            if (seen == chunk.file)
            {
                return catalogFailure(ErrorCode::InvalidData, "two chunks name the same file", kTileMapOperation);
            }
        }
        seenPaths.push_back(chunk.file);

        if (chunk.tileCount <= 0)
        {
            return catalogFailure(ErrorCode::InvalidData, "a chunk holds no tiles", kTileMapOperation);
        }
        const std::int64_t lastTile = static_cast<std::int64_t>(chunk.firstTile) + chunk.tileCount;
        if (chunk.firstTile < 0 || lastTile > static_cast<std::int64_t>(request.skyTileCount))
        {
            return catalogFailure(ErrorCode::InvalidData, "a chunk's tile range reaches outside the sky",
                                  kTileMapOperation);
        }
        for (std::int64_t tile = chunk.firstTile; tile < lastTile; ++tile)
        {
            std::uint32_t& owner = map.tileChunkIndices[static_cast<std::size_t>(tile)];
            if (owner != std::numeric_limits<std::uint32_t>::max())
            {
                return catalogFailure(ErrorCode::InvalidData, "two chunks hold the same tile", kTileMapOperation);
            }
            owner = static_cast<std::uint32_t>(position);
        }
        map.totalRecordCount += chunk.recordCount;
    }

    for (const std::uint32_t owner : map.tileChunkIndices)
    {
        if (owner == std::numeric_limits<std::uint32_t>::max())
        {
            return catalogFailure(ErrorCode::InvalidData, "the chunks do not cover the whole sky", kTileMapOperation);
        }
    }
    return map;
}

std::string makeGaiaTilePath(const GaiaCatalogMetadata& metadata, const std::int32_t tileIndex)
{
    if (tileIndex < 0)
    {
        return std::string{};
    }
    std::string name = std::to_string(tileIndex);
    if (name.size() < kTileNameDigits)
    {
        name.insert(name.begin(), static_cast<std::ptrdiff_t>(kTileNameDigits - name.size()), '0');
    }
    std::string path = metadata.tileDirectory;
    if (!path.empty())
    {
        path.push_back('/');
    }
    path.append("tile_");
    path.append(name);
    path.append(".bin");
    return path;
}

} // namespace an
