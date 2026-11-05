// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/GaiaCatalogSource.h"

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Algorithms/Catalog/GaiaChunkReader.h>
#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogRecord.h>
#include <AfterNightEngine/Algorithms/Catalog/Healpix.h>
#include <AfterNightEngine/Algorithms/Photometry/PhotometricColorCalibration.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QDir>
#include <QFile>

#include <cstddef>
#include <list>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace an::ui
{
namespace
{

/// A failure the catalog reports in its own words.
[[nodiscard]] an::Status refusal(const an::ErrorCode code, const QString& message)
{
    return an::Status::failure(code, an::StatusDetail{.message = message.toStdString()});
}

/// One chunk file, mapped for as long as the source lives.
struct MappedChunk
{
    std::unique_ptr<QFile> file;
    std::span<const std::byte> bytes;
    an::GaiaChunkLayout layout;
};

} // namespace

struct GaiaCatalogSource::State
{
    QString directory;
    an::GaiaCatalogMetadata metadata;
    an::HealpixGrid grid;
    an::GaiaTileMap tileMap;
    std::unordered_map<std::uint32_t, MappedChunk> chunks;

    /// Decoded tiles, the most recently used first, and where each sits in that order.
    std::list<std::pair<std::uint32_t, std::vector<an::GaiaStarRecord>>> tiles;
    std::unordered_map<std::uint32_t, decltype(tiles)::iterator> tileIndex;
    std::uint64_t tilesDecoded = 0u;

    ~State()
    {
        // A mapping is released before the file that holds it.
        for (auto& [index, chunk] : chunks)
        {
            if (chunk.file && !chunk.bytes.empty())
            {
                chunk.file->unmap(const_cast<uchar*>(reinterpret_cast<const uchar*>(chunk.bytes.data())));
            }
        }
    }

    /// The chunk that holds a tile, mapped and its index read on first use.
    [[nodiscard]] an::Result<const MappedChunk*> chunkFor(const an::ExecutionContext& execution,
                                                          const std::uint32_t chunkIndex)
    {
        if (const auto found = chunks.find(chunkIndex); found != chunks.end())
        {
            return &found->second;
        }
        if (chunkIndex >= metadata.chunks.size())
        {
            return refusal(an::ErrorCode::InvalidData, QStringLiteral("the tile map names a chunk the manifest lacks"));
        }
        const an::GaiaChunkDescriptor& descriptor = metadata.chunks[chunkIndex];
        const QString path = QDir(directory).filePath(QString::fromStdString(descriptor.file));
        auto file = std::make_unique<QFile>(path);
        if (!file->open(QIODevice::ReadOnly))
        {
            return refusal(an::ErrorCode::ExternalFailure,
                           QStringLiteral("the Gaia catalog chunk '%1' cannot be opened").arg(path));
        }
        const qint64 size = file->size();
        uchar* mapped = size > 0 ? file->map(0, size) : nullptr;
        if (mapped == nullptr)
        {
            return refusal(an::ErrorCode::ExternalFailure,
                           QStringLiteral("the Gaia catalog chunk '%1' cannot be mapped").arg(path));
        }
        MappedChunk chunk;
        chunk.bytes =
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(mapped), static_cast<std::size_t>(size));
        chunk.file = std::move(file);
        an::Result<an::GaiaChunkLayout> layout = an::readGaiaChunkLayout(execution, chunk.bytes, metadata, descriptor);
        if (!layout.hasValue())
        {
            chunk.file->unmap(mapped);
            return layout.status();
        }
        chunk.layout = std::move(*layout);
        const auto inserted = chunks.emplace(chunkIndex, std::move(chunk));
        return &inserted.first->second;
    }

    /// A tile's records, decoded on first use and kept while it is among the recently used.
    [[nodiscard]] an::Result<const std::vector<an::GaiaStarRecord>*> tileRecords(const an::ExecutionContext& execution,
                                                                                 const std::uint32_t tile)
    {
        if (const auto found = tileIndex.find(tile); found != tileIndex.end())
        {
            tiles.splice(tiles.begin(), tiles, found->second);
            return &tiles.front().second;
        }
        if (tile >= tileMap.tileChunkIndices.size())
        {
            return refusal(an::ErrorCode::InvalidData, QStringLiteral("a tile lies outside the catalog's map"));
        }
        an::Result<const MappedChunk*> chunk = chunkFor(execution, tileMap.tileChunkIndices[tile]);
        if (!chunk.hasValue())
        {
            return chunk.status();
        }
        const MappedChunk& mapped = **chunk;
        const an::Result<std::span<const std::byte>> bytes = an::gaiaChunkTileRecordBytes(
            mapped.bytes, mapped.layout, static_cast<std::int32_t>(tile) - mapped.layout.firstTile);
        if (!bytes.hasValue())
        {
            return bytes.status();
        }
        an::Result<std::vector<an::GaiaStarRecord>> records =
            an::decodeGaiaRecords(execution, *bytes, mapped.layout.recordLayout);
        if (!records.hasValue())
        {
            return records.status();
        }
        ++tilesDecoded;
        tiles.emplace_front(tile, std::move(*records));
        tileIndex[tile] = tiles.begin();
        while (tiles.size() > kGaiaCatalogTileCacheSize)
        {
            tileIndex.erase(tiles.back().first);
            tiles.pop_back();
        }
        return &tiles.front().second;
    }
};

GaiaCatalogSource::GaiaCatalogSource() noexcept = default;
GaiaCatalogSource::~GaiaCatalogSource() = default;
GaiaCatalogSource::GaiaCatalogSource(GaiaCatalogSource&& other) noexcept = default;
GaiaCatalogSource& GaiaCatalogSource::operator=(GaiaCatalogSource&& other) noexcept = default;

an::Result<GaiaCatalogSource> GaiaCatalogSource::open(const an::ExecutionContext& execution, const QString& directory)
{
    const QString manifestPath = QDir(directory).filePath(QString::fromLatin1(kGaiaCatalogManifestFileName));
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly))
    {
        return refusal(an::ErrorCode::ExternalFailure,
                       QStringLiteral("the Gaia catalog manifest '%1' cannot be read").arg(manifestPath));
    }
    const QByteArray text = manifestFile.read(static_cast<qint64>(an::kDefaultMaxGaiaManifestBytes) + 1);
    an::Result<an::ParsedGaiaManifest> manifest = an::parseGaiaCatalogManifest(
        execution, std::string_view(text.constData(), static_cast<std::size_t>(text.size())));
    if (!manifest.hasValue())
    {
        return manifest.status();
    }
    if (manifest->metadata.storage != an::GaiaCatalogStorage::Chunks)
    {
        return refusal(an::ErrorCode::Unsupported,
                       QStringLiteral("the Gaia catalog in '%1' is stored as tile files, which the plate solve no "
                                      "longer reads; download it again as a chunk catalog")
                           .arg(directory));
    }
    auto state = std::make_unique<State>();
    state->directory = directory;
    state->metadata = std::move(manifest->metadata);
    state->grid = an::HealpixGrid{.order = static_cast<std::uint8_t>(state->metadata.pixelisationOrder),
                                  .ordering = an::HealpixOrdering::Ring};
    if (state->metadata.pixelisationOrder < 0 || !an::isHealpixGrid(state->grid))
    {
        return refusal(an::ErrorCode::Unsupported,
                       QStringLiteral("the Gaia catalog states a tile order this build does not read"));
    }
    an::Result<an::GaiaTileMap> tileMap = an::buildGaiaTileMap(
        execution, state->metadata, an::GaiaTileMapRequest{.skyTileCount = an::healpixPixelCount(state->grid)});
    if (!tileMap.hasValue())
    {
        return tileMap.status();
    }
    state->tileMap = std::move(*tileMap);
    GaiaCatalogSource source;
    source.m_state = std::move(state);
    return source;
}

bool GaiaCatalogSource::valid() const noexcept
{
    return m_state != nullptr;
}

const an::GaiaCatalogMetadata& GaiaCatalogSource::metadata() const
{
    return m_state->metadata;
}

std::uint64_t GaiaCatalogSource::tilesDecoded() const noexcept
{
    return m_state ? m_state->tilesDecoded : 0u;
}

an::Result<an::SkyCatalog> GaiaCatalogSource::regionCatalog(const an::ExecutionContext& execution,
                                                            const an::SkyCoordinate& center, const double radiusDegrees,
                                                            const std::optional<double> observationEpochYear,
                                                            const std::optional<double> magnitudeLimit) const
{
    an::Result<GaiaPhotometricRegion> region =
        regionWithColors(execution, center, radiusDegrees, observationEpochYear, magnitudeLimit);
    if (!region.hasValue())
    {
        return region.status();
    }
    return std::move(region->catalog);
}

an::Result<GaiaPhotometricRegion> GaiaCatalogSource::regionWithColors(const an::ExecutionContext& execution,
                                                                      const an::SkyCoordinate& center,
                                                                      const double radiusDegrees,
                                                                      const std::optional<double> observationEpochYear,
                                                                      const std::optional<double> magnitudeLimit) const
{
    if (!m_state)
    {
        return refusal(an::ErrorCode::NotInitialized, QStringLiteral("no Gaia catalog is open"));
    }
    State& state = *m_state;
    an::Result<an::HealpixDiscResult> tiles =
        an::queryHealpixDisc(execution, state.grid,
                             an::HealpixDiscQuery{.center = center,
                                                  .radiusDegrees = radiusDegrees,
                                                  .coverage = an::HealpixDiscCoverage::ConservativeOverlap});
    if (!tiles.hasValue())
    {
        return tiles.status();
    }
    const double catalogEpoch = state.metadata.epochYear;
    std::vector<an::CatalogEntry> region;
    std::vector<std::optional<double>> colors;
    for (const an::HealpixPixelIndex& tile : tiles->pixels)
    {
        an::Result<const std::vector<an::GaiaStarRecord>*> records = state.tileRecords(execution, tile.value);
        if (!records.hasValue())
        {
            return records.status();
        }
        an::Result<std::vector<an::CatalogEntry>> entries = an::buildGaiaCatalogEntries(
            execution, **records,
            an::GaiaEntryRequest{.tileIndex = static_cast<std::int32_t>(tile.value),
                                 .catalogEpochYear = catalogEpoch,
                                 .observationEpochYear = observationEpochYear.value_or(catalogEpoch)});
        if (!entries.hasValue())
        {
            return entries.status();
        }
        // An entry is made for every record, in record order, so a record's colour is its entry's.
        const std::vector<std::optional<double>> tileColors = an::gaiaColorIndices(**records);
        if (tileColors.size() != entries->size())
        {
            return refusal(an::ErrorCode::InvalidData,
                           QStringLiteral("a Gaia tile's entries do not pair with its records"));
        }
        for (std::size_t index = 0u; index < entries->size(); ++index)
        {
            an::CatalogEntry& entry = (*entries)[index];
            if (magnitudeLimit.has_value() && (!entry.magnitude.has_value() || *entry.magnitude > *magnitudeLimit))
            {
                continue;
            }
            if (an::angularSeparationDegrees(center, entry.coordinate) > radiusDegrees)
            {
                continue;
            }
            if (region.size() >= an::kMaxCatalogEntries)
            {
                return an::Status::failure(
                    an::ErrorCode::LimitExceeded,
                    an::StatusDetail{.message = "the region holds more catalog stars than one solve reads; narrow the "
                                                "search radius or use a shallower catalog"});
            }
            region.push_back(std::move(entry));
            colors.push_back(tileColors[index]);
        }
    }
    if (region.empty())
    {
        return refusal(an::ErrorCode::EmptySelection, QStringLiteral("the Gaia catalog holds no star in the region"));
    }
    // The catalog keeps its entries in the order they are handed over, so the colours stay paired with them.
    an::Result<an::SkyCatalog> catalog = an::buildSkyCatalog(
        execution, std::move(region),
        an::SkyCatalogRequest{.photometricBand = an::kGaiaPhotometricBand, .buildNameIndex = false});
    if (!catalog.hasValue())
    {
        return catalog.status();
    }
    return GaiaPhotometricRegion{.catalog = std::move(*catalog), .colorIndexBpMinusRp = std::move(colors)};
}

} // namespace an::ui
