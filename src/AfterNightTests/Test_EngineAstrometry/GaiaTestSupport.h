// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogManifest.h>
#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogRecord.h>
#include <AfterNightEngine/Algorithms/Catalog/GaiaChunkReader.h>
#include <AfterNightEngine/Core/Status.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gaia_test
{

/// The bytes one star occupies in the widest layout, which the narrower two are prefixes of.
inline constexpr std::size_t kFullRecordBytes = 64u;

/// One star as a fixture writes it, before the absent codes are applied.
struct FixtureStar
{
    double rightAscensionDegrees = 0.0;
    double declinationDegrees = 0.0;
    std::uint64_t sourceIdentifier = 0u;
    float magnitudeG = 12.0f;
    float magnitudeBp = 12.5f;
    float magnitudeRp = 11.5f;
    float properMotionRightAscension = 0.0f;
    float properMotionDeclination = 0.0f;
    std::uint16_t temperature = 5800u;
    std::int16_t surfaceGravityHundredths = 445;
    std::int16_t metallicityHundredths = -12;
    std::uint16_t extinctionTenThousandths = 1200u;
    std::int16_t reddeningTenThousandths = 800;
};

/// Appends one little-endian unsigned value of a stated width.
inline void appendUnsigned(std::vector<std::byte>& bytes, std::uint64_t value, std::size_t width)
{
    for (std::size_t index = 0u; index < width; ++index)
    {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8u)) & 0xFFu));
    }
}

/// Appends one little-endian 32-bit float.
inline void appendFloat(std::vector<std::byte>& bytes, float value)
{
    appendUnsigned(bytes, std::bit_cast<std::uint32_t>(value), 4u);
}

/// Appends one little-endian 64-bit double.
inline void appendDouble(std::vector<std::byte>& bytes, double value)
{
    appendUnsigned(bytes, std::bit_cast<std::uint64_t>(value), 8u);
}

/// Appends one star in the stated layout.
inline void appendStar(std::vector<std::byte>& bytes, const FixtureStar& star, an::GaiaRecordLayout layout)
{
    appendDouble(bytes, star.rightAscensionDegrees);
    appendDouble(bytes, star.declinationDegrees);
    appendFloat(bytes, star.magnitudeG);
    appendFloat(bytes, star.magnitudeBp);
    appendFloat(bytes, star.magnitudeRp);
    appendFloat(bytes, star.properMotionRightAscension);
    appendFloat(bytes, star.properMotionDeclination);
    if (layout == an::GaiaRecordLayout::Version1)
    {
        return;
    }
    appendUnsigned(bytes, star.sourceIdentifier, 8u);
    if (layout == an::GaiaRecordLayout::Version2)
    {
        return;
    }
    appendUnsigned(bytes, star.temperature, 2u);
    appendUnsigned(bytes, static_cast<std::uint16_t>(star.surfaceGravityHundredths), 2u);
    appendUnsigned(bytes, static_cast<std::uint16_t>(star.metallicityHundredths), 2u);
    appendUnsigned(bytes, star.extinctionTenThousandths, 2u);
    appendUnsigned(bytes, static_cast<std::uint16_t>(star.reddeningTenThousandths), 2u);
    for (std::size_t index = 0u; index < 10u; ++index)
    {
        bytes.push_back(std::byte{0});
    }
}

/// The bytes a run of stars occupies in one layout.
[[nodiscard]] inline std::vector<std::byte> encodeStars(std::span<const FixtureStar> stars, an::GaiaRecordLayout layout)
{
    std::vector<std::byte> bytes;
    bytes.reserve(stars.size() * an::gaiaRecordSizeBytes(layout));
    for (const FixtureStar& star : stars)
    {
        appendStar(bytes, star, layout);
    }
    return bytes;
}

/// One tile of a fixture chunk.
struct FixtureTile
{
    std::vector<FixtureStar> stars{};
};

/// How a fixture chunk file may be written differently from the way one is.
struct ChunkFixtureOptions
{
    std::string catalogIdentifier = "gaia_dr3";
    std::string pixelOrdering = "ring";
    std::int32_t pixelisationOrder = 5;
    std::int32_t chunkIndex = 0;
    std::int32_t firstTile = 0;
    std::int32_t chunkCount = 1;
    std::int64_t recordVersion = 3;
    std::int64_t recordSizeBytes = 64;
    std::uint16_t containerVersion = 1u;
    std::uint16_t flags = 0u;
    std::uint32_t reserved = 0u;
    std::string magic = "ANSP";

    /// Written into the header instead of the number of stars the tiles actually hold.
    std::optional<std::uint64_t> statedRecordCount{};

    /// Written into the header instead of the number of tiles supplied.
    std::optional<std::int64_t> statedTileCount{};

    /// Written into the preamble instead of the header text's own length.
    std::optional<std::uint32_t> statedHeaderLength{};

    /// Replaces the header text entirely.
    std::optional<std::string> headerTextOverride{};
};

/// The header object a chunk file carries.
[[nodiscard]] inline std::string makeChunkHeaderText(const ChunkFixtureOptions& options, std::size_t tileCount,
                                                     std::uint64_t recordCount)
{
    const std::int64_t statedTiles =
        options.statedTileCount.has_value() ? *options.statedTileCount : static_cast<std::int64_t>(tileCount);
    const std::uint64_t statedRecords =
        options.statedRecordCount.has_value() ? *options.statedRecordCount : recordCount;
    std::string text = "{";
    text += "\"catalog_id\":\"" + options.catalogIdentifier + "\",";
    text += "\"pixel_ordering\":\"" + options.pixelOrdering + "\",";
    text += "\"healpix_order\":" + std::to_string(options.pixelisationOrder) + ",";
    text += "\"chunk_index\":" + std::to_string(options.chunkIndex) + ",";
    text += "\"first_tile\":" + std::to_string(options.firstTile) + ",";
    text += "\"tile_count\":" + std::to_string(statedTiles) + ",";
    text += "\"chunk_count\":" + std::to_string(options.chunkCount) + ",";
    text += "\"record_version\":" + std::to_string(options.recordVersion) + ",";
    text += "\"record_size_bytes\":" + std::to_string(options.recordSizeBytes) + ",";
    text += "\"total_stars_in_chunk\":" + std::to_string(statedRecords);
    text += "}";
    return text;
}

/// A whole chunk file: preamble, header, aligned tile index and record area.
[[nodiscard]] inline std::vector<std::byte> makeChunkFile(std::span<const FixtureTile> tiles,
                                                          const ChunkFixtureOptions& options = {})
{
    std::uint64_t recordCount = 0u;
    for (const FixtureTile& tile : tiles)
    {
        recordCount += tile.stars.size();
    }
    const std::string headerText = options.headerTextOverride.has_value()
                                       ? *options.headerTextOverride
                                       : makeChunkHeaderText(options, tiles.size(), recordCount);

    std::vector<std::byte> bytes;
    for (const char value : options.magic)
    {
        bytes.push_back(static_cast<std::byte>(value));
    }
    appendUnsigned(bytes, options.containerVersion, 2u);
    appendUnsigned(bytes, options.flags, 2u);
    appendUnsigned(bytes,
                   options.statedHeaderLength.has_value() ? *options.statedHeaderLength
                                                          : static_cast<std::uint32_t>(headerText.size()),
                   4u);
    appendUnsigned(bytes, options.reserved, 4u);
    for (const char value : headerText)
    {
        bytes.push_back(static_cast<std::byte>(value));
    }
    while ((bytes.size() % 8u) != 0u)
    {
        bytes.push_back(std::byte{0});
    }

    std::vector<std::byte> records;
    std::vector<std::uint64_t> offsets;
    offsets.reserve(tiles.size());
    for (const FixtureTile& tile : tiles)
    {
        offsets.push_back(records.size());
        const std::vector<std::byte> encoded = encodeStars(tile.stars, an::GaiaRecordLayout::Version3);
        records.insert(records.end(), encoded.begin(), encoded.end());
    }

    for (std::size_t index = 0u; index < tiles.size(); ++index)
    {
        appendUnsigned(bytes, static_cast<std::uint32_t>(tiles[index].stars.size()), 4u);
        appendUnsigned(bytes, offsets[index], 8u);
    }
    bytes.insert(bytes.end(), records.begin(), records.end());
    return bytes;
}

/// A manifest describing one chunked catalog of the supplied chunk descriptions.
[[nodiscard]] inline std::string makeChunkManifestText(std::span<const an::GaiaChunkDescriptor> chunks,
                                                       const std::string& extraMembers = {})
{
    std::string text = "{";
    text += "\"format_version\":3,\"storage\":\"chunks\",\"record_size_bytes\":64,";
    text += "\"healpix_order\":5,\"pixel_ordering\":\"ring\",";
    text += "\"catalog_name\":\"Gaia DR3 Local Catalog\",\"catalog_id\":\"gaia_dr3\",";
    text += "\"epoch_year\":2016.0,\"magnitude_limit\":14.0,";
    if (!extraMembers.empty())
    {
        text += extraMembers;
        text += ",";
    }
    text += "\"chunks\":[";
    for (std::size_t index = 0u; index < chunks.size(); ++index)
    {
        const an::GaiaChunkDescriptor& chunk = chunks[index];
        if (index != 0u)
        {
            text += ",";
        }
        text += "{\"index\":" + std::to_string(chunk.index);
        text += ",\"file\":\"" + chunk.file + "\"";
        text += ",\"first_tile\":" + std::to_string(chunk.firstTile);
        text += ",\"tile_count\":" + std::to_string(chunk.tileCount);
        text += ",\"stars\":" + std::to_string(chunk.recordCount);
        text += ",\"size_bytes\":" + std::to_string(chunk.sizeBytes);
        text += ",\"sha256\":\"" + chunk.checksum + "\"}";
    }
    text += "]}";
    return text;
}

/// Where a fixture chunk's tile index begins, which is what a test corrupts to break one entry.
[[nodiscard]] inline std::size_t chunkIndexOffset(std::span<const std::byte> chunkBytes)
{
    std::uint32_t headerLength = 0u;
    for (std::size_t index = 0u; index < 4u; ++index)
    {
        headerLength |= static_cast<std::uint32_t>(chunkBytes[8u + index]) << (index * 8u);
    }
    const std::size_t headerEnd = 16u + headerLength;
    return (headerEnd + 7u) & ~std::size_t{7};
}

/// Writes one little-endian unsigned value over bytes a fixture already produced.
inline void patchUnsigned(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value, std::size_t width)
{
    for (std::size_t index = 0u; index < width; ++index)
    {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8u)) & 0xFFu);
    }
}

/// The count a failure attached under one key, or a value no field carries.
[[nodiscard]] inline std::uint64_t contextCount(const an::Status& status, an::ContextKey key)
{
    const an::StatusContextField* const field = status.findContext(key);
    return (field == nullptr || field->kind != an::ContextValueKind::Count) ? ~std::uint64_t{0} : field->count;
}

/// The text a failure attached under one key, or a value no field carries.
[[nodiscard]] inline std::string contextText(const an::Status& status, an::ContextKey key)
{
    const an::StatusContextField* const field = status.findContext(key);
    return (field == nullptr || field->kind != an::ContextValueKind::Text) ? std::string{"<no-field>"} : field->text;
}

} // namespace gaia_test
