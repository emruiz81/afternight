// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The two catalogs the product ships as resources - the deep-sky objects and the bright stars - read into the engine's
/// sky catalogs.
///
/// Each is read by whoever needs it, on a worker - a view's annotation overlay, the catalog search - and lives as long
/// as that owner does: a sky catalog holds a lease on the engine context it was built in, and a catalog nobody owned
/// would keep the session from shutting down.
///
/// The texts are read by the engine's catalog text parser (`parseCatalogText`), which reads a magnitude of 90 or more
/// as "not measured", and built with their name index (`buildSkyCatalog`).
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>
#include <AfterNightEngine/Core/Result.h>

#include <QByteArray>

#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string_view>

namespace an
{
class ExecutionContext;
} // namespace an

namespace an::ui
{

/// One of the catalogs the product ships.
enum class BundledCatalog : std::uint8_t
{
    /// The deep-sky objects: galaxies, nebulae and clusters, with their sizes.
    DeepSky,

    /// The bright stars, to visual magnitude 9, with their proper names and Bayer and Flamsteed designations.
    BrightStars
};

/// The resource a bundled catalog is read from.
///
/// @param catalog which catalog.
/// @return the Qt resource path.
[[nodiscard]] AFTERNIGHTCONTROLS_API const char* bundledCatalogResourcePath(BundledCatalog catalog) noexcept;

/// A bundled catalog's text, read from the product's resources.
///
/// @param catalog which catalog.
/// @return the text; `ErrorCode::ExternalFailure` naming the resource when it cannot be read.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<QByteArray> bundledCatalogText(BundledCatalog catalog);

/// Where a reader of the bundled catalogs takes their text from: `bundledCatalogText` unless its owner names another
/// source - a copy of its own, or a test's catalog. Called on a worker.
using BundledCatalogSource = std::function<an::Result<QByteArray>(BundledCatalog catalog)>;

/// A catalog text in a bundled catalog's columns, read into a sky catalog.
///
/// A reader that only asks what lies in a field builds no name index: the index holds about twice what the entries
/// do - some 30 MiB for the bright stars - and only a search by name reads it.
///
/// @param execution the operation reading it.
/// @param catalog which catalog's columns the text has.
/// @param text the text.
/// @param nameIndex whether to build the index a search by name reads.
/// @return the catalog; or what the text parse or the catalog build refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::SkyCatalog>
buildBundledCatalog(const an::ExecutionContext& execution, BundledCatalog catalog, std::string_view text,
                    bool nameIndex = true);

/// A bundled catalog read from the product's resources, under an operation admitted in the session. For a worker: the
/// admission waits.
///
/// @param session the process's session.
/// @param catalog which catalog.
/// @param stop the read's stop.
/// @param source where the text comes from; empty reads the product's resources.
/// @param nameIndex whether to build the index a search by name reads; a reader of fields alone leaves it out.
/// @return the catalog; or what the text read, the admission, the text parse or the catalog build refused.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::SkyCatalog>
loadBundledCatalog(const an::consumer::EngineSession& session, BundledCatalog catalog, std::stop_token stop,
                   const BundledCatalogSource& source = {}, bool nameIndex = true);

/// The region query a bundled catalog is asked for an overlay: at a limit, the deep-sky objects admit a named object
/// nobody measured a magnitude for, and the bright stars do not; with none, every entry of the region is admitted.
///
/// @param catalog which catalog.
/// @param center the cone's centre.
/// @param radiusDegrees the cone's radius.
/// @param magnitudeLimit the faintest magnitude admitted, or nothing for every magnitude, measured or not.
/// @return the query.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::CatalogRegionQuery
bundledCatalogRegionQuery(BundledCatalog catalog, const an::SkyCoordinate& center, double radiusDegrees,
                          std::optional<double> magnitudeLimit);

} // namespace an::ui
