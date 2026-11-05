// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The internal plate solve's reference catalog: the stars of an installed Gaia chunk catalog inside a cone, carried to
/// an observation epoch, as the engine's sky catalog.
///
/// The engine reads a Gaia manifest, a chunk's index and its records from bytes and opens no file; this is the
/// product's half. It reads the manifest once and proves its chunks cover the sky (`parseGaiaCatalogManifest`,
/// `buildGaiaTileMap`), turns a cone into the catalog's tiles through the engine's HEALPix disc query - conservative,
/// so a star near a tile's edge is never lost - maps each chunk file a tile lives in and hands the engine the mapping
/// (`readGaiaChunkLayout`, `gaiaChunkTileRecordBytes`, `decodeGaiaRecords`), and builds the stars inside the cone,
/// carried to the epoch and held to a magnitude limit, into a `SkyCatalog` (`buildGaiaCatalogEntries`,
/// `buildSkyCatalog`) the engine's plate solver queries. A solve asks for its region once. The photometric colour
/// calibration asks for the same region with each star's BP minus RP colour beside its entry (`regionWithColors`,
/// `gaiaColorIndices`).
///
/// A source is used by one thread at a time: the solve that opened it.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightEngine/Algorithms/Catalog/GaiaCatalogManifest.h>
#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/SkyCoordinate.h>

#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace an
{
class ExecutionContext;
} // namespace an

namespace an::ui
{

/// The name of the manifest beside a Gaia catalog's chunks.
inline constexpr const char kGaiaCatalogManifestFileName[] = "catalog_meta.json";

/// How many decoded tiles a source keeps between the regions it is asked for.
inline constexpr std::uint32_t kGaiaCatalogTileCacheSize = 256u;

/// The catalog's stars inside a cone and the BP minus RP colour of each, which the photometric colour calibration
/// predicts its channel ratios from.
struct GaiaPhotometricRegion
{
    /// The stars, as `GaiaCatalogSource::regionCatalog` answers them.
    an::SkyCatalog catalog;

    /// One colour index per entry of the catalog, in entry order, absent where either band is unmeasured.
    std::vector<std::optional<double>> colorIndexBpMinusRp;
};

/// One installed Gaia chunk catalog, read for regional catalogs.
class AFTERNIGHTCONTROLS_API GaiaCatalogSource
{
public:
    /// A source of nothing.
    GaiaCatalogSource() noexcept;
    ~GaiaCatalogSource();
    GaiaCatalogSource(GaiaCatalogSource&& other) noexcept;
    GaiaCatalogSource& operator=(GaiaCatalogSource&& other) noexcept;
    GaiaCatalogSource(const GaiaCatalogSource&) = delete;
    GaiaCatalogSource& operator=(const GaiaCatalogSource&) = delete;

    /// Opens the catalog in a directory: reads its manifest and builds the map from tiles to chunks.
    ///
    /// @param execution the operation reading it.
    /// @param directory the catalog's directory, holding `catalog_meta.json` and the chunk files it names.
    /// @return the source; `ErrorCode::ExternalFailure` naming the manifest when it cannot be read;
    ///         `ErrorCode::Unsupported` for a catalog stored as tile files rather than chunks; or what the manifest
    ///         parse or the tile map refused.
    [[nodiscard]] static an::Result<GaiaCatalogSource> open(const an::ExecutionContext& execution,
                                                            const QString& directory);

    /// Whether this source holds a catalog.
    [[nodiscard]] bool valid() const noexcept;

    /// What the catalog states about itself. Calling it on a source of nothing is a defect.
    [[nodiscard]] const an::GaiaCatalogMetadata& metadata() const;

    /// The catalog's stars inside a cone, carried to an epoch, as a sky catalog without a name index.
    ///
    /// @param execution the operation reading it.
    /// @param center the cone's centre.
    /// @param radiusDegrees the cone's radius, finite and positive.
    /// @param observationEpochYear the epoch to carry the stars to, or nothing to keep the catalog's own.
    /// @param magnitudeLimit the faintest G magnitude kept, or nothing to keep every star with a magnitude.
    /// @return the catalog; `ErrorCode::EmptySelection` when the cone holds no star; `ErrorCode::ExternalFailure`
    /// naming
    ///         a chunk file that cannot be opened or mapped; `ErrorCode::LimitExceeded` when the cone holds more stars
    ///         than one catalog holds; or what the disc query, a chunk read, a decode or the catalog build refused.
    [[nodiscard]] an::Result<an::SkyCatalog> regionCatalog(const an::ExecutionContext& execution,
                                                           const an::SkyCoordinate& center, double radiusDegrees,
                                                           std::optional<double> observationEpochYear,
                                                           std::optional<double> magnitudeLimit) const;

    /// `regionCatalog`, with the BP minus RP colour of every star it answers, paired with its entry.
    ///
    /// @param execution the operation reading it.
    /// @param center the cone's centre.
    /// @param radiusDegrees the cone's radius, finite and positive.
    /// @param observationEpochYear the epoch to carry the stars to, or nothing to keep the catalog's own.
    /// @param magnitudeLimit the faintest G magnitude kept, or nothing to keep every star with a magnitude.
    /// @return the catalog and its colours; or what `regionCatalog` refuses.
    [[nodiscard]] an::Result<GaiaPhotometricRegion>
    regionWithColors(const an::ExecutionContext& execution, const an::SkyCoordinate& center, double radiusDegrees,
                     std::optional<double> observationEpochYear, std::optional<double> magnitudeLimit) const;

    /// How many tiles the source has decoded since it opened; a tile kept between regions is decoded once.
    [[nodiscard]] std::uint64_t tilesDecoded() const noexcept;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

} // namespace an::ui
