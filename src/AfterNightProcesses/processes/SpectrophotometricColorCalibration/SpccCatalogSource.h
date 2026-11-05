// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The installed SPCC catalog, read for one field: its manifest, the chunks the field and a set of Gaia sources need,
/// and the spectrum catalog the calibration matches against - all of it, or only the sources an earlier stage matched.
///
/// The engine parses a manifest and decodes a chunk from bytes and opens no file; this is the product's half. A field
/// needs the chunks whose tiles its cone meets (`selectSpectrumChunksForRegion`) and the chunks the integration
/// hand-off's sources live in (`selectSpectrumChunksForSources`); each is read whole and decoded
/// (`decodeSpectrumChunk`), and the catalog is built once from what was decoded (`buildSpectrumCatalog`). A subset
/// catalog is built from the same chunks with only the named sources' records kept, which is how the calibration is
/// confined to the stars the registration matched.
#pragma once

#include "AfterNightProcessesExport.h"

#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/SkyCoordinate.h>

#include <QString>

#include <cstdint>
#include <span>
#include <vector>

namespace an
{
class ExecutionContext;
} // namespace an

namespace an::ui::processes
{

/// The name of the manifest beside an SPCC catalog's chunks.
inline constexpr const char kSpccCatalogManifestFileName[] = "catalog_meta.json";

/// The chunks of an installed catalog one field needs, decoded.
struct SpccCatalogRegion
{
    /// The catalog's manifest.
    an::SpectrumCatalogManifest manifest;

    /// The decoded chunks, in ascending chunk order.
    std::vector<an::SpectrumChunk> chunks;

    /// Which chunks the field's cone needs, and whether the manifest declares all of them.
    an::SpectrumChunkSelection selection;
};

/// Reads an installed catalog's manifest.
///
/// @param execution the operation reading it.
/// @param directory the catalog's directory.
/// @return the manifest; `ErrorCode::ExternalFailure` naming the manifest when it cannot be read; or what the engine's
///         parse refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::SpectrumCatalogManifest>
readSpccCatalogManifest(const an::ExecutionContext& execution, const QString& directory);

/// Reads and decodes the chunks a field and a set of Gaia sources need.
///
/// @param execution the operation reading them.
/// @param directory the catalog's directory.
/// @param manifest the catalog's manifest.
/// @param center the field's centre.
/// @param radiusDegrees the cone's radius, finite and positive.
/// @param sourceIdentifiers Gaia sources whose chunks are read too, when installed; a source chunk that is not
///        installed is passed over, and the sources in it are not found.
/// @return the chunks; `ErrorCode::ExternalFailure` naming a chunk the field needs that cannot be read;
///         `ErrorCode::EmptySelection` when the cone needs no chunk the manifest declares; or what the selection or a
///         decode refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<SpccCatalogRegion>
readSpccCatalogRegion(const an::ExecutionContext& execution, const QString& directory,
                      const an::SpectrumCatalogManifest& manifest, const an::SkyCoordinate& center,
                      double radiusDegrees, std::span<const std::uint64_t> sourceIdentifiers = {});

/// The spectrum catalog of a region's chunks.
///
/// @param execution the operation building it.
/// @param region the decoded chunks.
/// @return the catalog; or what the engine's build refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::SpectrumCatalog>
buildSpccCatalog(const an::ExecutionContext& execution, const SpccCatalogRegion& region);

/// The spectrum catalog of only the named sources of a region's chunks.
///
/// @param execution the operation building it.
/// @param region the decoded chunks.
/// @param sourceIdentifiers the sources kept; one the chunks do not hold is passed over.
/// @return the catalog; or what the engine's build refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::SpectrumCatalog>
buildSpccSubsetCatalog(const an::ExecutionContext& execution, const SpccCatalogRegion& region,
                       std::span<const std::uint64_t> sourceIdentifiers);

} // namespace an::ui::processes
