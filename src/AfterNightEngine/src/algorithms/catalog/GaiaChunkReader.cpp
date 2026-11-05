// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - reading the container a chunked Gaia catalog stores its tiles in.

#include <AfterNightEngine/Algorithms/Catalog/GaiaChunkReader.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "algorithms/catalog/CatalogInternal.h"
#include "algorithms/catalog/CatalogJson.h"
#include "algorithms/catalog/GaiaBinary.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace an
{
namespace
{

using catalog_detail::catalogFailure;
using catalog_detail::catalogLimitFailure;
using catalog_detail::findJsonMember;
using catalog_detail::jsonInteger;
using catalog_detail::JsonLimits;
using catalog_detail::JsonMember;
using catalog_detail::JsonScanStatus;
using catalog_detail::jsonString;
using catalog_detail::jsonUnsigned;
using catalog_detail::readUint16LittleEndian;
using catalog_detail::readUint32LittleEndian;
using catalog_detail::readUint64LittleEndian;
using catalog_detail::scanJsonObject;

constexpr const char* kLayoutOperation = "an::readGaiaChunkLayout";
constexpr const char* kTileBytesOperation = "an::gaiaChunkTileRecordBytes";
constexpr std::uint32_t kCancellationStride = 4096u;

/// Where each field of the fixed preamble begins, in bytes from the start of the file.
constexpr std::size_t kMagicOffset = 0u;
constexpr std::size_t kContainerVersionOffset = 4u;
constexpr std::size_t kFlagsOffset = 6u;
constexpr std::size_t kHeaderLengthOffset = 8u;
constexpr std::size_t kReservedOffset = 12u;

/// What the index table is aligned to, in bytes, after the header text.
constexpr std::uint64_t kIndexAlignment = 8u;

/// The record layout a chunk container is defined for.
constexpr GaiaRecordLayout kChunkRecordLayout = GaiaRecordLayout::Version3;

/// The container's own record-version number for that layout.
constexpr std::int64_t kChunkRecordVersion = 3;

/// Builds a failure that names the chunk file it is about.
///
/// @param code why the read failed.
/// @param message what a person reads.
/// @param file the chunk's path as the manifest stated it.
/// @return the failure.
[[nodiscard]] Status chunkFailure(const ErrorCode code, const char* message, const std::string& file)
{
    return Status::failure(code, StatusDetail{.message = message,
                                              .operation = kLayoutOperation,
                                              .domain = ErrorDomain::Algorithms,
                                              .context = {StatusContextField::ofText(ContextKey::Path, file)}});
}

/// Reads a whole-number member the container header must state.
///
/// @param members the header's members.
/// @param name the member to read.
/// @param value receives the number.
/// @return true when the member is present and is a whole number in range.
[[nodiscard]] bool readRequiredInteger(const std::span<const JsonMember> members, const std::string_view name,
                                       std::int64_t& value)
{
    const JsonMember* const member = findJsonMember(members, name);
    if (member == nullptr)
    {
        return false;
    }
    const std::optional<std::int64_t> parsed = jsonInteger(member->value);
    if (!parsed.has_value())
    {
        return false;
    }
    value = *parsed;
    return true;
}

/// Reads a text member the container header must state.
///
/// @param members the header's members.
/// @param name the member to read.
/// @param value receives the text.
/// @return true when the member is present and is well-formed text.
[[nodiscard]] bool readRequiredText(const std::span<const JsonMember> members, const std::string_view name,
                                    std::string& value)
{
    const JsonMember* const member = findJsonMember(members, name);
    if (member == nullptr)
    {
        return false;
    }
    std::optional<std::string> parsed = jsonString(member->value);
    if (!parsed.has_value())
    {
        return false;
    }
    value = *parsed;
    return true;
}

} // namespace

Result<GaiaChunkLayout> readGaiaChunkLayout(const ExecutionContext& execution,
                                            const std::span<const std::byte> chunkBytes,
                                            const GaiaCatalogMetadata& metadata, const GaiaChunkDescriptor& descriptor,
                                            const GaiaChunkLimits& limits)
{
    if (!execution.valid())
    {
        return catalogFailure(ErrorCode::NotInitialized, "the operation has no engine context", kLayoutOperation);
    }
    if (metadata.storage != GaiaCatalogStorage::Chunks)
    {
        return catalogFailure(ErrorCode::Unsupported, "only a chunk-stored catalog holds chunk files",
                              kLayoutOperation);
    }
    if (limits.maximumHeaderBytes == 0u || limits.maximumTiles == 0u)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "a chunk limit admits nothing", kLayoutOperation);
    }
    if (descriptor.index < 0 || static_cast<std::size_t>(descriptor.index) >= metadata.chunks.size() ||
        metadata.chunks[static_cast<std::size_t>(descriptor.index)] != descriptor)
    {
        return catalogFailure(ErrorCode::InvalidArgument, "the descriptor is not one of this catalog's chunks",
                              kLayoutOperation);
    }
    if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
    {
        return cancelled;
    }

    const std::uint64_t fileSize = chunkBytes.size();
    if (descriptor.sizeBytes != 0u && descriptor.sizeBytes != fileSize)
    {
        return chunkFailure(ErrorCode::InvalidData,
                            "the chunk holds a different number of bytes than the manifest"
                            " states",
                            descriptor.file);
    }
    if (fileSize < kGaiaChunkPreambleBytes)
    {
        return chunkFailure(ErrorCode::InvalidData, "the chunk is too short to hold a preamble", descriptor.file);
    }

    const std::string_view magic{reinterpret_cast<const char*>(chunkBytes.data() + kMagicOffset), 4u};
    if (magic != std::string_view{kGaiaChunkMagic})
    {
        return chunkFailure(ErrorCode::InvalidData, "the chunk does not begin with the container's magic bytes",
                            descriptor.file);
    }
    const std::uint16_t containerVersion = readUint16LittleEndian(chunkBytes, kContainerVersionOffset);
    if (containerVersion != kGaiaChunkContainerVersion)
    {
        return chunkFailure(ErrorCode::Unsupported, "the chunk states a container version this build does not read",
                            descriptor.file);
    }
    if (readUint16LittleEndian(chunkBytes, kFlagsOffset) != 0u ||
        readUint32LittleEndian(chunkBytes, kReservedOffset) != 0u)
    {
        return chunkFailure(ErrorCode::Unsupported, "the chunk sets a preamble field this build does not understand",
                            descriptor.file);
    }

    const std::uint32_t headerLength = readUint32LittleEndian(chunkBytes, kHeaderLengthOffset);
    if (headerLength > limits.maximumHeaderBytes)
    {
        return catalogLimitFailure("the chunk's header text is larger than the limit admits", kLayoutOperation,
                                   headerLength, limits.maximumHeaderBytes);
    }
    const std::uint64_t headerEnd = static_cast<std::uint64_t>(kGaiaChunkPreambleBytes) + headerLength;
    if (headerEnd > fileSize)
    {
        return chunkFailure(ErrorCode::InvalidData, "the chunk's header reaches past the end of the file",
                            descriptor.file);
    }

    const std::string_view headerText{reinterpret_cast<const char*>(chunkBytes.data() + kGaiaChunkPreambleBytes),
                                      headerLength};
    std::vector<JsonMember> members;
    if (const JsonScanStatus status =
            scanJsonObject(headerText, JsonLimits{.maximumEntries = 4096u, .maximumDepth = 16u}, members);
        status != JsonScanStatus::Ok)
    {
        return chunkFailure(ErrorCode::InvalidData, "the chunk's header is not one object of members", descriptor.file);
    }

    std::int64_t chunkIndex = 0;
    std::int64_t firstTile = 0;
    std::int64_t tileCount = 0;
    std::int64_t chunkCount = 0;
    std::int64_t recordVersion = 0;
    std::int64_t recordSizeBytes = 0;
    std::int64_t pixelisationOrder = 0;
    std::string catalogIdentifier;
    std::string pixelOrdering;
    if (!readRequiredInteger(members, "chunk_index", chunkIndex) ||
        !readRequiredInteger(members, "first_tile", firstTile) ||
        !readRequiredInteger(members, "tile_count", tileCount) ||
        !readRequiredInteger(members, "chunk_count", chunkCount) ||
        !readRequiredInteger(members, "record_version", recordVersion) ||
        !readRequiredInteger(members, "record_size_bytes", recordSizeBytes) ||
        !readRequiredInteger(members, "healpix_order", pixelisationOrder) ||
        !readRequiredText(members, "catalog_id", catalogIdentifier) ||
        !readRequiredText(members, "pixel_ordering", pixelOrdering))
    {
        return chunkFailure(ErrorCode::InvalidData, "the chunk's header does not state everything a chunk states",
                            descriptor.file);
    }
    std::optional<std::uint64_t> statedRecordCount;
    if (const JsonMember* const member = findJsonMember(members, "total_stars_in_chunk"); member != nullptr)
    {
        const std::optional<std::uint64_t> parsed = jsonUnsigned(member->value);
        if (!parsed.has_value())
        {
            return chunkFailure(ErrorCode::InvalidData, "the chunk's header states a record total that is not a count",
                                descriptor.file);
        }
        statedRecordCount = *parsed;
    }

    if (catalogIdentifier != metadata.catalogIdentifier)
    {
        return chunkFailure(ErrorCode::InvalidData, "the chunk belongs to a different catalog", descriptor.file);
    }
    if (pixelOrdering != metadata.pixelOrdering ||
        pixelisationOrder != static_cast<std::int64_t>(metadata.pixelisationOrder))
    {
        return chunkFailure(ErrorCode::InvalidData, "the chunk is tiled differently from the catalog that names it",
                            descriptor.file);
    }
    if (recordVersion != kChunkRecordVersion ||
        recordSizeBytes != static_cast<std::int64_t>(gaiaRecordSizeBytes(kChunkRecordLayout)))
    {
        return chunkFailure(ErrorCode::InvalidData, "the chunk states a record layout chunks are not written with",
                            descriptor.file);
    }
    if (chunkIndex != descriptor.index || firstTile != descriptor.firstTile || tileCount != descriptor.tileCount)
    {
        return chunkFailure(ErrorCode::InvalidData,
                            "the chunk's header and the manifest disagree about which chunk"
                            " this is",
                            descriptor.file);
    }
    if (chunkCount != static_cast<std::int64_t>(metadata.chunks.size()))
    {
        return chunkFailure(ErrorCode::InvalidData,
                            "the chunk's header and the manifest disagree about how many"
                            " chunks there are",
                            descriptor.file);
    }
    if (tileCount <= 0)
    {
        return chunkFailure(ErrorCode::InvalidData, "the chunk holds no tiles", descriptor.file);
    }
    if (static_cast<std::uint64_t>(tileCount) > limits.maximumTiles)
    {
        return catalogLimitFailure("the chunk holds more tiles than the limit admits", kLayoutOperation,
                                   static_cast<std::uint64_t>(tileCount), limits.maximumTiles);
    }

    const std::uint64_t indexStart = (headerEnd + (kIndexAlignment - 1u)) & ~(kIndexAlignment - 1u);
    const std::uint64_t indexBytes = static_cast<std::uint64_t>(tileCount) * kGaiaChunkIndexEntryBytes;
    if (indexStart > fileSize || indexBytes > fileSize - indexStart)
    {
        return chunkFailure(ErrorCode::InvalidData, "the chunk's tile index reaches past the end of the file",
                            descriptor.file);
    }

    GaiaChunkLayout layout;
    layout.chunkIndex = descriptor.index;
    layout.firstTile = descriptor.firstTile;
    layout.recordLayout = kChunkRecordLayout;
    layout.recordAreaOffset = indexStart + indexBytes;
    layout.tiles.resize(static_cast<std::size_t>(tileCount));

    const std::uint64_t recordSize = gaiaRecordSizeBytes(kChunkRecordLayout);
    const std::uint64_t recordAreaBytes = fileSize - layout.recordAreaOffset;
    std::uint64_t previousEnd = 0u;
    std::uint64_t totalRecords = 0u;
    for (std::size_t tile = 0u; tile < layout.tiles.size(); ++tile)
    {
        if ((tile % kCancellationStride) == 0u)
        {
            if (const Status cancelled = execution.checkCancellation(); cancelled.failed())
            {
                return cancelled;
            }
        }
        const std::size_t entryOffset = static_cast<std::size_t>(indexStart) + (tile * kGaiaChunkIndexEntryBytes);
        GaiaChunkTileSpan& span = layout.tiles[tile];
        span.recordCount = readUint32LittleEndian(chunkBytes, entryOffset);
        span.recordAreaOffset = readUint64LittleEndian(chunkBytes, entryOffset + 4u);

        if (tile == 0u && span.recordAreaOffset != 0u)
        {
            return chunkFailure(ErrorCode::InvalidData, "the chunk's first tile does not begin the record area",
                                descriptor.file);
        }
        if (span.recordAreaOffset < previousEnd || span.recordAreaOffset % recordSize != 0u)
        {
            return chunkFailure(ErrorCode::InvalidData, "the chunk's tile records overlap or are misaligned",
                                descriptor.file);
        }

        const std::uint64_t tileBytes = static_cast<std::uint64_t>(span.recordCount) * recordSize;
        if (span.recordAreaOffset > recordAreaBytes || tileBytes > recordAreaBytes - span.recordAreaOffset)
        {
            return chunkFailure(ErrorCode::InvalidData, "a tile's records reach past the end of the chunk",
                                descriptor.file);
        }
        previousEnd = span.recordAreaOffset + tileBytes;
        totalRecords += span.recordCount;
    }

    if ((statedRecordCount.has_value() && totalRecords != *statedRecordCount) || totalRecords != descriptor.recordCount)
    {
        return chunkFailure(ErrorCode::InvalidData,
                            "the chunk's tiles, its header and the manifest disagree about how"
                            " many records it holds",
                            descriptor.file);
    }
    layout.totalRecordCount = totalRecords;
    return layout;
}

Result<std::span<const std::byte>> gaiaChunkTileRecordBytes(const std::span<const std::byte> chunkBytes,
                                                            const GaiaChunkLayout& layout,
                                                            const std::int32_t localTileIndex)
{
    if (localTileIndex < 0 || static_cast<std::size_t>(localTileIndex) >= layout.tiles.size())
    {
        return catalogFailure(ErrorCode::OutOfRange, "the chunk holds no tile at that position", kTileBytesOperation);
    }

    const GaiaChunkTileSpan& span = layout.tiles[static_cast<std::size_t>(localTileIndex)];
    const std::uint64_t recordSize = gaiaRecordSizeBytes(layout.recordLayout);
    const std::uint64_t tileBytes = static_cast<std::uint64_t>(span.recordCount) * recordSize;
    const std::uint64_t begin = layout.recordAreaOffset + span.recordAreaOffset;
    if (begin > chunkBytes.size() || tileBytes > chunkBytes.size() - begin)
    {
        return catalogFailure(ErrorCode::InvalidData, "the layout does not describe the bytes it was given",
                              kTileBytesOperation);
    }
    return chunkBytes.subspan(static_cast<std::size_t>(begin), static_cast<std::size_t>(tileBytes));
}

} // namespace an
