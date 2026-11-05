// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A sampled-spectrum catalog fixture: manifest text and chunk containers written byte by byte in the
// format the engine reads, so a test can state a catalog and every way one can be malformed.

#pragma once

#include <AfterNightEngine/Algorithms/Catalog/Healpix.h>
#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace spectrum_test
{

/// One star as a fixture writes it.
struct FixtureSpectrumStar
{
    std::uint64_t sourceIdentifier = 1u;
    double rightAscensionDegrees = 0.0;
    double declinationDegrees = 0.0;
    float properMotionRightAscension = std::numeric_limits<float>::quiet_NaN();
    float properMotionDeclination = std::numeric_limits<float>::quiet_NaN();
    float referenceEpochYear = 2016.0f;
    float magnitudeG = 12.0f;
    float magnitudeBp = 12.4f;
    float magnitudeRp = 11.6f;
    float fluxScale = 1.0e-18f;
    float fluxErrorScale = 1.0e-20f;
    std::uint16_t qualityFlags = 0u;
    std::uint16_t validSampleCount = an::kXpSampledWavelengthCount;
    std::uint8_t sourceKind = 0u;

    /// The quantized flux written for sample `index`: a ramp offset by the star's ordinal, so two
    /// stars' spectra can be told apart.
    std::int16_t quantizedFlux(std::uint32_t index) const noexcept
    {
        return static_cast<std::int16_t>(100 + static_cast<int>(index % 200u) +
                                         static_cast<int>(sourceIdentifier % 7u));
    }

    /// The quantized flux error written for sample `index`.
    std::uint16_t quantizedError(std::uint32_t index) const noexcept
    {
        return static_cast<std::uint16_t>(5u + index % 3u);
    }
};

/// One chunk as a fixture manifest declares it.
struct FixtureChunk
{
    std::string file = "chunks/chunk-0000.anxp";
    std::int32_t firstTile = 0;
    std::int32_t tileCount = 0;
    std::vector<FixtureSpectrumStar> stars{};
    std::uint64_t sizeBytes = 0u;
};

/// How a fixture manifest may differ from a correct one.
struct ManifestOptions
{
    std::string schema = an::kXpCatalogManifestSchema;
    std::string family = an::kXpCatalogFamilyId;
    std::string manifestId = "manifest-test-001";
    std::string accessProfile = "local-full";
    std::string sourceKind = "gaia_dr3_xp_sampled";
    std::string magic = "ANXP";
    int formatVersion = 1;
    int recordVersion = 1;
    std::string byteOrder = "little-endian";
    std::uint64_t recordSizeBytes = an::kXpChunkRecordBytes;
    int healpixOrder = an::kXpCatalogHealpixOrder;
    int chunkParentOrder = an::kXpChunkParentHealpixOrder;
    std::uint32_t sampleCount = an::kXpSampledWavelengthCount;
    double startNm = an::kXpSampledWavelengthStartNm;
    double stepNm = an::kXpSampledWavelengthStepNm;
    double endNm = an::kXpSampledWavelengthEndNm;
    std::string reviewStatus = "approved-for-bundling";
    std::optional<std::uint64_t> statedTotalRecords{};
    std::string chunkManifestFile{};
    std::string sourceIndexFile{};
};

/// A number spelled for a JSON document.
[[nodiscard]] inline std::string jsonNumber(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return buffer;
}

/// The manifest text a fixture catalog is described by.
[[nodiscard]] inline std::string makeManifestText(const std::vector<FixtureChunk>& chunks,
                                                  const ManifestOptions& options = {})
{
    std::uint64_t total = 0u;
    for (const FixtureChunk& chunk : chunks)
    {
        total += chunk.stars.size();
    }
    std::string text = "{";
    text += "\"schema_version\":\"" + options.schema + "\",";
    text += "\"catalog_family_id\":\"" + options.family + "\",";
    text += "\"manifest_id\":\"" + options.manifestId + "\",";
    text += "\"access_profile\":\"" + options.accessProfile + "\",";
    text += "\"source_kind\":\"" + options.sourceKind + "\",";
    text += "\"payload_kind\":\"sampled_spectra\",";
    text += "\"total_records\":" + std::to_string(options.statedTotalRecords.value_or(total)) + ",";
    text += "\"flux_unit\":\"W m-2 nm-1\",";
    text += "\"rights\":{\"review_status\":\"" + options.reviewStatus + "\"},";
    text += "\"files\":{";
    if (!options.chunkManifestFile.empty())
    {
        text += "\"chunk_manifest\":\"" + options.chunkManifestFile + "\",";
    }
    if (!options.sourceIndexFile.empty())
    {
        text += "\"source_id_index\":\"" + options.sourceIndexFile + "\",";
    }
    text += "\"coverage_summary\":\"coverage_summary.json\"},";
    text += "\"format\":{\"magic\":\"" + options.magic +
            "\",\"format_version\":" + std::to_string(options.formatVersion) +
            ",\"record_version\":" + std::to_string(options.recordVersion) + ",\"byte_order\":\"" + options.byteOrder +
            "\",\"record_size_bytes\":" + std::to_string(options.recordSizeBytes) +
            ",\"healpix_order\":" + std::to_string(options.healpixOrder) +
            ",\"chunk_parent_healpix_order\":" + std::to_string(options.chunkParentOrder) + "},";
    text += "\"wavelength_grid\":{\"sample_count\":" + std::to_string(options.sampleCount) +
            ",\"start_nm\":" + jsonNumber(options.startNm) + ",\"step_nm\":" + jsonNumber(options.stepNm) +
            ",\"end_nm\":" + jsonNumber(options.endNm) + "},";
    text += "\"chunks\":[";
    for (std::size_t index = 0u; index < chunks.size(); ++index)
    {
        const FixtureChunk& chunk = chunks[index];
        if (index > 0u)
        {
            text += ",";
        }
        text += "{\"chunk_index\":" + std::to_string(index) + ",\"file\":\"" + chunk.file +
                "\",\"first_tile\":" + std::to_string(chunk.firstTile) +
                ",\"tile_count\":" + std::to_string(chunk.tileCount) +
                ",\"record_count\":" + std::to_string(chunk.stars.size()) +
                ",\"size_bytes\":" + std::to_string(chunk.sizeBytes) + ",\"sha256\":\"\"}";
    }
    text += "]}";
    return text;
}

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

/// How a fixture chunk container may differ from a correct one.
struct ChunkOptions
{
    std::string magic = "ANXP";
    std::uint16_t formatVersion = 1u;
    std::uint16_t recordVersion = 1u;
    std::optional<std::uint32_t> statedRecordCount{};
    std::optional<std::string> headerOverride{};
    std::optional<std::string> headerFamily{};
    std::optional<std::uint64_t> headerRecordCount{};
    std::size_t extraTrailingBytes = 0u;
};

/// One chunk container's bytes.
[[nodiscard]] inline std::vector<std::byte> makeChunkBytes(const FixtureChunk& chunk, std::size_t chunkIndex,
                                                           const ManifestOptions& manifest = {},
                                                           const ChunkOptions& options = {})
{
    std::string header;
    if (options.headerOverride.has_value())
    {
        header = *options.headerOverride;
    }
    else
    {
        header = "{\"catalog_family_id\":\"" + options.headerFamily.value_or(manifest.family) +
                 "\",\"manifest_id\":\"" + manifest.manifestId + "\",\"chunk_index\":" + std::to_string(chunkIndex) +
                 ",\"record_count\":" + std::to_string(options.headerRecordCount.value_or(chunk.stars.size())) +
                 ",\"record_size_bytes\":" + std::to_string(an::kXpChunkRecordBytes) +
                 ",\"first_tile\":" + std::to_string(chunk.firstTile) +
                 ",\"tile_count\":" + std::to_string(chunk.tileCount) + "}";
    }
    std::vector<std::byte> bytes;
    for (const char value : options.magic)
    {
        bytes.push_back(static_cast<std::byte>(value));
    }
    appendUnsigned(bytes, options.formatVersion, 2u);
    appendUnsigned(bytes, options.recordVersion, 2u);
    appendUnsigned(bytes, header.size(), 4u);
    appendUnsigned(bytes, options.statedRecordCount.value_or(static_cast<std::uint32_t>(chunk.stars.size())), 4u);
    for (const char value : header)
    {
        bytes.push_back(static_cast<std::byte>(value));
    }
    for (const FixtureSpectrumStar& star : chunk.stars)
    {
        const std::size_t begin = bytes.size();
        appendUnsigned(bytes, star.sourceIdentifier, 8u);
        appendDouble(bytes, star.rightAscensionDegrees);
        appendDouble(bytes, star.declinationDegrees);
        appendFloat(bytes, star.properMotionRightAscension);
        appendFloat(bytes, star.properMotionDeclination);
        appendFloat(bytes, star.referenceEpochYear);
        appendFloat(bytes, star.magnitudeG);
        appendFloat(bytes, star.magnitudeBp);
        appendFloat(bytes, star.magnitudeRp);
        appendFloat(bytes, star.fluxScale);
        appendFloat(bytes, star.fluxErrorScale);
        appendUnsigned(bytes, star.qualityFlags, 2u);
        appendUnsigned(bytes, star.validSampleCount, 2u);
        bytes.push_back(static_cast<std::byte>(star.sourceKind));
        bytes.push_back(std::byte{0});
        for (std::uint32_t index = 0u; index < an::kXpSampledWavelengthCount; ++index)
        {
            appendUnsigned(bytes, static_cast<std::uint16_t>(star.quantizedFlux(index)), 2u);
        }
        for (std::uint32_t index = 0u; index < an::kXpSampledWavelengthCount; ++index)
        {
            appendUnsigned(bytes, star.quantizedError(index), 2u);
        }
        while (bytes.size() - begin < an::kXpChunkRecordBytes)
        {
            bytes.push_back(std::byte{0});
        }
    }
    for (std::size_t index = 0u; index < options.extraTrailingBytes; ++index)
    {
        bytes.push_back(std::byte{0});
    }
    return bytes;
}

/// The order-six nested tile a direction falls in.
[[nodiscard]] inline std::uint32_t tileSixOf(double rightAscensionDegrees, double declinationDegrees)
{
    const an::Result<an::HealpixPixelIndex> pixel = an::skyCoordinateToHealpixPixel(
        an::HealpixGrid{.order = an::kXpCatalogHealpixOrder, .ordering = an::HealpixOrdering::Nested},
        an::SkyCoordinate{.rightAscensionDegrees = rightAscensionDegrees, .declinationDegrees = declinationDegrees});
    EXPECT_TRUE(pixel.hasValue()) << pixel.status().message();
    return pixel.hasValue() ? pixel->value : 0u;
}

/// A Gaia-shaped identifier whose top bits name the order-twelve tile of a direction.
[[nodiscard]] inline std::uint64_t identifierAt(double rightAscensionDegrees, double declinationDegrees,
                                                std::uint64_t ordinal)
{
    const an::Result<an::HealpixPixelIndex> pixel = an::skyCoordinateToHealpixPixel(
        an::HealpixGrid{.order = an::kGaiaSourceIdentifierHealpixOrder, .ordering = an::HealpixOrdering::Nested},
        an::SkyCoordinate{.rightAscensionDegrees = rightAscensionDegrees, .declinationDegrees = declinationDegrees});
    EXPECT_TRUE(pixel.hasValue()) << pixel.status().message();
    const std::uint64_t tile = pixel.hasValue() ? pixel->value : 0u;
    return tile * an::kGaiaSourceIdentifierTileDivisor + ordinal + 1u;
}

/// A star at a direction with a consistent identifier.
[[nodiscard]] inline FixtureSpectrumStar starAt(double rightAscensionDegrees, double declinationDegrees,
                                                std::uint64_t ordinal, float magnitudeG = 12.0f)
{
    FixtureSpectrumStar star;
    star.sourceIdentifier = identifierAt(rightAscensionDegrees, declinationDegrees, ordinal);
    star.rightAscensionDegrees = rightAscensionDegrees;
    star.declinationDegrees = declinationDegrees;
    star.magnitudeG = magnitudeG;
    return star;
}

/// A parsed manifest, failing the test rather than the caller when the parse refuses.
[[nodiscard]] inline an::SpectrumCatalogManifest parseOrFail(const an::ExecutionContext& execution,
                                                             const std::vector<FixtureChunk>& chunks,
                                                             const ManifestOptions& options = {})
{
    const an::Result<an::SpectrumCatalogManifest> parsed =
        an::parseSpectrumCatalogManifest(execution, makeManifestText(chunks, options));
    EXPECT_TRUE(parsed.hasValue()) << parsed.status().message();
    return parsed.hasValue() ? *parsed : an::SpectrumCatalogManifest{};
}

/// A whole catalog from fixture chunks.
[[nodiscard]] inline an::SpectrumCatalog buildOrFail(const an::ExecutionContext& execution,
                                                     const std::vector<FixtureChunk>& chunks,
                                                     const ManifestOptions& options = {})
{
    const an::SpectrumCatalogManifest manifest = parseOrFail(execution, chunks, options);
    std::vector<an::SpectrumChunk> decoded;
    for (std::size_t index = 0u; index < chunks.size(); ++index)
    {
        const std::vector<std::byte> bytes = makeChunkBytes(chunks[index], index, options);
        an::Result<an::SpectrumChunk> chunk =
            an::decodeSpectrumChunk(execution, bytes, manifest, static_cast<std::uint32_t>(index));
        EXPECT_TRUE(chunk.hasValue()) << chunk.status().message();
        if (chunk.hasValue())
        {
            decoded.push_back(std::move(*chunk));
        }
    }
    an::Result<an::SpectrumCatalog> built = an::buildSpectrumCatalog(execution, std::move(decoded), manifest);
    EXPECT_TRUE(built.hasValue()) << built.status().message();
    return built.hasValue() ? std::move(*built) : an::SpectrumCatalog{};
}

} // namespace spectrum_test
