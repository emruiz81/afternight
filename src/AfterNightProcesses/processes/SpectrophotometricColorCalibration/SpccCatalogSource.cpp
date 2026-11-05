// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The installed SPCC catalog's manifest and chunks read for one field, and its catalogs built.

#include "stdafx.hpp"

#include "processes/SpectrophotometricColorCalibration/SpccCatalogSource.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QDir>
#include <QFile>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace an::ui::processes
{

namespace
{

[[nodiscard]] an::Status refusal(const an::ErrorCode code, const QString& message)
{
    return an::Status::failure(code, an::StatusDetail{.message = message.toStdString()});
}

/// A chunk's file read whole and decoded.
[[nodiscard]] an::Result<an::SpectrumChunk> readChunk(const an::ExecutionContext& execution, const QDir& root,
                                                      const an::SpectrumCatalogManifest& manifest,
                                                      const std::uint32_t chunkIndex)
{
    const QString path = root.filePath(QString::fromStdString(manifest.chunks[chunkIndex].file));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return refusal(an::ErrorCode::ExternalFailure,
                       QStringLiteral("the SPCC catalog chunk '%1' cannot be read").arg(path));
    }
    const QByteArray bytes = file.readAll();
    return an::decodeSpectrumChunk(execution,
                                   std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.constData()),
                                                              static_cast<std::size_t>(bytes.size())),
                                   manifest, chunkIndex);
}

/// A copy of a chunk keeping only the named sources' records and their spectra.
[[nodiscard]] an::SpectrumChunk subsetOf(const an::SpectrumChunk& chunk, std::span<const std::uint64_t> sorted)
{
    an::SpectrumChunk kept;
    kept.chunkIndex = chunk.chunkIndex;
    kept.grid = chunk.grid;
    const std::size_t samples = chunk.grid.sampleCount;
    for (std::size_t index = 0u; index < chunk.records.size(); ++index)
    {
        if (!std::binary_search(sorted.begin(), sorted.end(), chunk.records[index].sourceIdentifier))
        {
            continue;
        }
        kept.records.push_back(chunk.records[index]);
        const auto from = static_cast<std::ptrdiff_t>(index * samples);
        const auto to = static_cast<std::ptrdiff_t>((index + 1u) * samples);
        kept.flux.insert(kept.flux.end(), chunk.flux.begin() + from, chunk.flux.begin() + to);
        kept.fluxError.insert(kept.fluxError.end(), chunk.fluxError.begin() + from, chunk.fluxError.begin() + to);
    }
    return kept;
}

} // namespace

an::Result<an::SpectrumCatalogManifest> readSpccCatalogManifest(const an::ExecutionContext& execution,
                                                                const QString& directory)
{
    const QString path = QDir(directory).filePath(QString::fromLatin1(kSpccCatalogManifestFileName));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return refusal(an::ErrorCode::ExternalFailure,
                       QStringLiteral("the SPCC catalog manifest '%1' cannot be read").arg(path));
    }
    const QByteArray text = file.read(static_cast<qint64>(an::kDefaultMaxSpectrumManifestBytes) + 1);
    return an::parseSpectrumCatalogManifest(execution,
                                            std::string_view(text.constData(), static_cast<std::size_t>(text.size())));
}

an::Result<SpccCatalogRegion> readSpccCatalogRegion(const an::ExecutionContext& execution, const QString& directory,
                                                    const an::SpectrumCatalogManifest& manifest,
                                                    const an::SkyCoordinate& center, const double radiusDegrees,
                                                    std::span<const std::uint64_t> sourceIdentifiers)
{
    an::Result<an::SpectrumChunkSelection> selection =
        an::selectSpectrumChunksForRegion(execution, manifest, center, radiusDegrees);
    if (!selection.hasValue())
    {
        return selection.status();
    }
    if (selection->chunkIndices.empty())
    {
        return refusal(an::ErrorCode::EmptySelection,
                       QStringLiteral("the SPCC catalog declares no chunk for this image's field"));
    }
    an::Result<std::vector<std::uint32_t>> sourceChunks =
        an::selectSpectrumChunksForSources(execution, manifest, sourceIdentifiers);
    if (!sourceChunks.hasValue())
    {
        return sourceChunks.status();
    }

    const QDir root(directory);
    SpccCatalogRegion region;
    region.manifest = manifest;
    region.selection = std::move(*selection);
    std::vector<std::uint32_t> wanted = region.selection.chunkIndices;
    for (const std::uint32_t chunk : *sourceChunks)
    {
        if (std::find(wanted.begin(), wanted.end(), chunk) == wanted.end() &&
            QFile::exists(root.filePath(QString::fromStdString(manifest.chunks[chunk].file))))
        {
            wanted.push_back(chunk);
        }
    }
    std::sort(wanted.begin(), wanted.end());
    for (const std::uint32_t chunkIndex : wanted)
    {
        an::Result<an::SpectrumChunk> chunk = readChunk(execution, root, manifest, chunkIndex);
        if (!chunk.hasValue())
        {
            return chunk.status();
        }
        region.chunks.push_back(std::move(*chunk));
    }
    return region;
}

an::Result<an::SpectrumCatalog> buildSpccCatalog(const an::ExecutionContext& execution, const SpccCatalogRegion& region)
{
    return an::buildSpectrumCatalog(execution, region.chunks, region.manifest);
}

an::Result<an::SpectrumCatalog> buildSpccSubsetCatalog(const an::ExecutionContext& execution,
                                                       const SpccCatalogRegion& region,
                                                       std::span<const std::uint64_t> sourceIdentifiers)
{
    std::vector<std::uint64_t> sorted(sourceIdentifiers.begin(), sourceIdentifiers.end());
    std::sort(sorted.begin(), sorted.end());
    std::vector<an::SpectrumChunk> chunks;
    for (const an::SpectrumChunk& chunk : region.chunks)
    {
        chunks.push_back(subsetOf(chunk, sorted));
    }
    return an::buildSpectrumCatalog(execution, std::move(chunks), region.manifest);
}

} // namespace an::ui::processes
