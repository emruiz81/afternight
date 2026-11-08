// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/BundledCatalogs.h"

#include <AfterNightEngine/Algorithms/Catalog/CatalogText.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <QFile>
#include <QString>

#include <string>
#include <utility>

namespace an::ui
{

const char* bundledCatalogResourcePath(const BundledCatalog catalog) noexcept
{
    return catalog == BundledCatalog::DeepSky ? ":/AfterNight/data/deep_sky_catalog.csv"
                                              : ":/AfterNight/data/bright_star_catalog.csv";
}

an::Result<QByteArray> bundledCatalogText(const BundledCatalog catalog)
{
    QFile file(QString::fromLatin1(bundledCatalogResourcePath(catalog)));
    if (!file.open(QIODevice::ReadOnly))
    {
        return an::Status::failure(an::ErrorCode::ExternalFailure,
                                   an::StatusDetail{.message = std::string("the bundled catalog '") +
                                                               bundledCatalogResourcePath(catalog) +
                                                               "' cannot be read"});
    }
    return file.readAll();
}

an::Result<an::SkyCatalog> buildBundledCatalog(const an::ExecutionContext& execution, const BundledCatalog catalog,
                                               const std::string_view text, const bool nameIndex)
{
    an::CatalogTextRequest request;
    request.schema =
        catalog == BundledCatalog::DeepSky ? an::CatalogTextSchema::ExtendedObject : an::CatalogTextSchema::Star;
    an::Result<an::ParsedCatalogText> parsed = an::parseCatalogText(execution, text, request);
    if (!parsed.hasValue())
    {
        return parsed.status();
    }
    an::SkyCatalogRequest built;
    built.photometricBand = catalog == BundledCatalog::BrightStars ? "V" : "";
    built.buildNameIndex = nameIndex;
    return an::buildSkyCatalog(execution, std::move(parsed->entries), built);
}

an::Result<an::SkyCatalog> loadBundledCatalog(const an::consumer::EngineSession& session, const BundledCatalog catalog,
                                              std::stop_token stop, const BundledCatalogSource& source,
                                              const bool nameIndex)
{
    const an::Result<QByteArray> text = source ? source(catalog) : bundledCatalogText(catalog);
    if (!text.hasValue())
    {
        return text.status();
    }
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview, .stopToken = stop});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    return buildBundledCatalog(scope->execution(), catalog,
                               std::string_view(text->constData(), static_cast<std::size_t>(text->size())), nameIndex);
}

an::CatalogRegionQuery bundledCatalogRegionQuery(const BundledCatalog catalog, const an::SkyCoordinate& center,
                                                 const double radiusDegrees, const std::optional<double> magnitudeLimit)
{
    an::CatalogRegionQuery query;
    query.center = center;
    query.radiusDegrees = radiusDegrees;
    query.magnitudeLimit = magnitudeLimit;
    query.admitNamedEntriesWithoutMagnitude = catalog == BundledCatalog::DeepSky;
    return query;
}

} // namespace an::ui
