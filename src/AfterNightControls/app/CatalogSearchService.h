// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <verdigris/wobjectdefs.h>

#include "app/BundledCatalogs.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Algorithms/Catalog/SkyCatalog.h>

#include <memory>
#include <stop_token>

namespace an
{
class ExecutionContext;
} // namespace an

namespace an::ui
{

enum class CatalogServer
{
    Simbad,
    CdsSesame,
    VizierSesame,
    LocalCatalog,
};

struct CatalogSearchEntry
{
    QString objectName;
    QString objectType;
    double raDeg = 0.0;
    double decDeg = 0.0;
    QString resolver;
    QString description;
    QStringList alternateNames;
};

struct CatalogSearchResult
{
    bool success = false;
    QString errorMessage;
    QVector<CatalogSearchEntry> entries;
};

namespace catalog_search
{

[[nodiscard]] AFTERNIGHTCONTROLS_API QString displayName(CatalogServer server);
[[nodiscard]] AFTERNIGHTCONTROLS_API QUrl buildRequestUrl(CatalogServer server, const QString& objectName);
[[nodiscard]] AFTERNIGHTCONTROLS_API QByteArray buildRequestBody(CatalogServer server, const QString& objectName);
[[nodiscard]] AFTERNIGHTCONTROLS_API CatalogSearchResult parseResponse(CatalogServer server, const QByteArray& payload);

/// The bundled catalogs a local search reads, as loaded: a catalog that could not be read is empty and says why.
struct LocalCatalogs
{
    an::SkyCatalog deepSky;
    QString deepSkyError;
    an::SkyCatalog brightStars;
    QString brightStarsError;
};

/// Reads both bundled catalogs in the session. For a worker: the reads are admitted.
[[nodiscard]] AFTERNIGHTCONTROLS_API LocalCatalogs loadLocalCatalogs(const an::consumer::EngineSession& session,
                                                                     const std::stop_token& stop,
                                                                     const BundledCatalogSource& source = {});

/// What the bundled catalogs answer for a name: the deep-sky objects first, a typed designation read as its catalog
/// spells it, then the bright stars by name as written.
[[nodiscard]] AFTERNIGHTCONTROLS_API CatalogSearchResult searchLocalCatalogs(const an::ExecutionContext& execution,
                                                                             const LocalCatalogs& catalogs,
                                                                             const QString& objectName);

/// Reads the bundled catalogs and searches them for a name, for a worker that searches once.
[[nodiscard]] AFTERNIGHTCONTROLS_API CatalogSearchResult searchLocalCatalog(const an::consumer::EngineSession& session,
                                                                            const QString& objectName,
                                                                            const std::stop_token& stop);
[[nodiscard]] AFTERNIGHTCONTROLS_API QStringList updatedRecentHistory(QStringList existingHistory,
                                                                      const QString& acceptedObjectName,
                                                                      int maxEntries = 20);

} // namespace catalog_search

class AFTERNIGHTCONTROLS_API CatalogSearchService : public QObject
{
    W_OBJECT(CatalogSearchService)

public:
    explicit CatalogSearchService(QObject* parent = nullptr);
    ~CatalogSearchService() override;

    void searchByName(const QString& objectName, CatalogServer server);
    void cancel();

    /// Reads the local catalogs' text from another source than the product's resources; catalogs already read are
    /// read again by the next local search.
    void setLocalCatalogSource(BundledCatalogSource source);

    void searchFinished(CatalogSearchResult result) W_SIGNAL(searchFinished, result);

private:
    void handleReplyFinished();

    struct impl;
    std::unique_ptr<impl> m_impl;
};

} // namespace an::ui

Q_DECLARE_METATYPE(an::ui::CatalogSearchEntry)
Q_DECLARE_METATYPE(an::ui::CatalogSearchResult)
W_REGISTER_ARGTYPE(an::ui::CatalogSearchResult)
