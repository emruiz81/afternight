// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/CatalogSearchService.h"

#include "app/AppContextHelpers.h"
#include "app/BundledCatalogs.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightPlatform/Logging.h>

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QCoreApplication>
#include <QFile>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSslSocket>
#include <QTimer>
#include <QThreadPool>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr auto kCatalogSearchTimeoutMs = 15000;
constexpr auto kSimbadTapUrl = "https://simbad.cds.unistra.fr/simbad/sim-tap/sync";
constexpr auto kCdsSesameUrl = "https://cds.unistra.fr/cgi-bin/nph-sesame/-oxI/A?";
constexpr auto kVizierSesameUrl = "https://vizier.cds.unistra.fr/viz-bin/nph-sesame/-oxI/A?";

/// The process's session, or null when no context lends one.
[[nodiscard]] const an::consumer::EngineSession* currentSession()
{
    if (auto* provider = process_utils::currentEngineSessionProvider())
    {
        return &provider->engineSession();
    }
    return nullptr;
}

/// A catalog's entries matching a name, in catalog order.
[[nodiscard]] std::vector<const an::CatalogEntry*> entriesNamed(const an::ExecutionContext& execution,
                                                                const an::SkyCatalog& catalog, const std::string& name,
                                                                const bool normalizeDesignation)
{
    std::vector<const an::CatalogEntry*> found;
    if (!catalog.valid())
    {
        return found;
    }
    an::CatalogNameQuery query;
    query.text = name;
    query.normalizeDesignation = normalizeDesignation;
    const an::Result<an::CatalogQueryResult> answered = an::queryCatalogByName(execution, catalog, query);
    if (!answered.hasValue())
    {
        return found;
    }
    for (const std::uint32_t index : answered->entryIndices)
    {
        if (const an::CatalogEntry* entry = catalog.entry(index); entry != nullptr)
        {
            found.push_back(entry);
        }
    }
    return found;
}

[[nodiscard]] QString trimQuoted(QString value)
{
    value = value.trimmed();
    if ((value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))) ||
        (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))))
    {
        if (value.size() >= 2)
        {
            value = value.mid(1, value.size() - 2);
        }
    }
    return value.trimmed();
}

[[nodiscard]] QString normalizedObjectName(QString value)
{
    value = trimQuoted(std::move(value));
    value = value.simplified();
    return value;
}

// ADQL string literals use single-quote delimiters; the only required escape
// is doubling embedded single quotes (''). No other injection vectors exist
// because ADQL has no backslash-escape or comment syntax within string literals.
[[nodiscard]] QString adqlEscapedObjectName(QString object_name)
{
    object_name = normalizedObjectName(std::move(object_name));
    object_name.replace(QStringLiteral("'"), QStringLiteral("''"));
    return object_name;
}

[[nodiscard]] QString userAgentValue()
{
    QString version = QCoreApplication::applicationVersion().trimmed();
    if (version.isEmpty())
    {
        version = QStringLiteral("dev");
    }
    return QStringLiteral("AfterNight/%1").arg(version);
}

[[nodiscard]] QString tlsUnavailableUserMessage()
{
    return QCoreApplication::translate(
        "CatalogSearchService",
        "Secure catalog access is unavailable because TLS/SSL support could not be initialized. "
        "Ensure the Qt TLS plugins and OpenSSL runtime libraries are installed or deployed, then restart AfterNight.");
}

[[nodiscard]] QString tlsDiagnosticsSummary()
{
    const QStringList available_backends = QSslSocket::availableBackends();
    const QString active_backend = QSslSocket::activeBackend().trimmed();
    const QString build_ssl = QSslSocket::sslLibraryBuildVersionString().trimmed();
    const QString runtime_ssl = QSslSocket::sslLibraryVersionString().trimmed();
    const QStringList library_paths = QCoreApplication::libraryPaths();

    return QStringLiteral(
               "supportsSsl=%1 activeBackend=%2 availableBackends=%3 buildSsl=%4 runtimeSsl=%5 libraryPaths=%6")
        .arg(QSslSocket::supportsSsl() ? QStringLiteral("true") : QStringLiteral("false"),
             active_backend.isEmpty() ? QStringLiteral("(none)") : active_backend,
             available_backends.isEmpty() ? QStringLiteral("(none)") : available_backends.join(QStringLiteral(",")),
             build_ssl.isEmpty() ? QStringLiteral("(unknown)") : build_ssl,
             runtime_ssl.isEmpty() ? QStringLiteral("(unknown)") : runtime_ssl,
             library_paths.isEmpty() ? QStringLiteral("(none)") : library_paths.join(QStringLiteral(",")));
}

[[nodiscard]] an::ui::CatalogSearchResult makeErrorResult(const QString& message)
{
    an::ui::CatalogSearchResult result;
    result.success = false;
    result.errorMessage = message;
    return result;
}

[[nodiscard]] bool parseCoordinatePair(const QString& raw_value, double& ra_deg, double& dec_deg)
{
    const QString simplified = raw_value.simplified();
    if (simplified.isEmpty())
    {
        return false;
    }

    const QStringList parts = simplified.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (parts.size() < 2)
    {
        return false;
    }

    bool ra_ok = false;
    bool dec_ok = false;
    const double parsed_ra = parts.at(0).toDouble(&ra_ok);
    const double parsed_dec = parts.at(1).toDouble(&dec_ok);
    if (!ra_ok || !dec_ok)
    {
        return false;
    }

    ra_deg = parsed_ra;
    dec_deg = parsed_dec;
    return true;
}

[[nodiscard]] bool parseDoubleValue(const QString& raw_value, double& value)
{
    bool ok = false;
    const double parsed = raw_value.trimmed().toDouble(&ok);
    if (!ok)
    {
        return false;
    }

    value = parsed;
    return true;
}

[[nodiscard]] QStringList uniqueNormalizedNames(const QStringList& values, const QString& excluded = {})
{
    QStringList normalized;
    normalized.reserve(values.size());

    const QString excluded_name = normalizedObjectName(excluded);
    for (const QString& value : values)
    {
        const QString candidate = normalizedObjectName(value);
        if (candidate.isEmpty())
        {
            continue;
        }

        if (!excluded_name.isEmpty() && candidate.compare(excluded_name, Qt::CaseInsensitive) == 0)
        {
            continue;
        }

        const auto duplicate =
            std::find_if(normalized.cbegin(), normalized.cend(), [&candidate](const QString& existing) {
                return existing.compare(candidate, Qt::CaseInsensitive) == 0;
            });
        if (duplicate == normalized.cend())
        {
            normalized.push_back(candidate);
        }
    }

    return normalized;
}

[[nodiscard]] QStringList parseSimbadAliasArray(QString raw_value)
{
    raw_value = trimQuoted(std::move(raw_value));
    if (raw_value.isEmpty())
    {
        return {};
    }

    if (!raw_value.startsWith(QLatin1Char('{')) || !raw_value.endsWith(QLatin1Char('}')))
    {
        return uniqueNormalizedNames({raw_value});
    }

    const QString body = raw_value.mid(1, raw_value.size() - 2);
    QStringList values;
    QString current;
    bool in_quotes = false;
    bool escaping = false;

    for (const QChar ch : body)
    {
        if (escaping)
        {
            current.append(ch);
            escaping = false;
            continue;
        }

        if (in_quotes)
        {
            if (ch == QLatin1Char('\\'))
            {
                escaping = true;
            }
            else if (ch == QLatin1Char('"'))
            {
                in_quotes = false;
            }
            else
            {
                current.append(ch);
            }
            continue;
        }

        if (ch == QLatin1Char('"'))
        {
            in_quotes = true;
        }
        else if (ch == QLatin1Char(','))
        {
            values.push_back(current);
            current.clear();
        }
        else
        {
            current.append(ch);
        }
    }
    values.push_back(current);

    return uniqueNormalizedNames(values);
}

[[nodiscard]] QString takeNamedAliasDescription(QStringList& aliases)
{
    for (int i = 0; i < aliases.size(); ++i)
    {
        const QString alias = aliases.at(i).simplified();
        if (!alias.startsWith(QStringLiteral("NAME "), Qt::CaseInsensitive))
        {
            continue;
        }

        const QString description = alias.mid(5).simplified();
        aliases.removeAt(i);
        return description;
    }

    return {};
}

[[nodiscard]] QString joinDescriptionAndNotes(const QString& description, const QStringList& note_lines)
{
    QStringList parts;
    const QString trimmed_description = description.trimmed();
    if (!trimmed_description.isEmpty())
    {
        parts.push_back(trimmed_description);
    }

    for (const QString& note : note_lines)
    {
        const QString trimmed_note = note.trimmed();
        if (!trimmed_note.isEmpty())
        {
            parts.push_back(trimmed_note);
        }
    }

    return parts.join(QLatin1Char('\n'));
}

[[nodiscard]] an::ui::CatalogSearchResult parseSimbadResponse(const QByteArray& payload)
{
    const QString text = QString::fromUtf8(payload);
    const QStringList raw_lines = text.split(QLatin1Char('\n'));

    QStringList header_columns;
    QVector<an::ui::CatalogSearchEntry> entries;

    for (QString line : raw_lines)
    {
        line.replace(QLatin1Char('\r'), QString());
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
        {
            continue;
        }

        const QStringList columns = line.split(QLatin1Char('\t'));
        if (header_columns.isEmpty())
        {
            header_columns = columns;
            continue;
        }

        if (columns.size() != header_columns.size())
        {
            return makeErrorResult(QStringLiteral("SIMBAD returned a malformed TSV row."));
        }

        const auto index_of = [&](const QString& name) -> int {
            for (int i = 0; i < header_columns.size(); ++i)
            {
                if (header_columns.at(i).trimmed().compare(name, Qt::CaseInsensitive) == 0)
                {
                    return i;
                }
            }
            return -1;
        };

        const int ra_index = index_of(QStringLiteral("ra"));
        const int dec_index = index_of(QStringLiteral("dec"));
        const int name_index = index_of(QStringLiteral("main_id"));
        const int type_index = index_of(QStringLiteral("otype_longname"));
        const int type_codes_index = index_of(QStringLiteral("type_codes"));
        const int aliases_index = index_of(QStringLiteral("aliases"));
        if (ra_index < 0 || dec_index < 0 || name_index < 0 || type_index < 0)
        {
            return makeErrorResult(QStringLiteral("SIMBAD TSV response is missing required columns."));
        }

        bool ra_ok = false;
        bool dec_ok = false;
        const double ra_deg = columns.at(ra_index).trimmed().toDouble(&ra_ok);
        const double dec_deg = columns.at(dec_index).trimmed().toDouble(&dec_ok);
        if (!ra_ok || !dec_ok)
        {
            return makeErrorResult(QStringLiteral("SIMBAD returned invalid coordinates."));
        }

        an::ui::CatalogSearchEntry entry;
        entry.objectName = normalizedObjectName(columns.at(name_index));
        entry.objectType = trimQuoted(columns.at(type_index));
        entry.raDeg = ra_deg;
        entry.decDeg = dec_deg;
        entry.resolver = QStringLiteral("SIMBAD");
        if (aliases_index >= 0)
        {
            entry.alternateNames = parseSimbadAliasArray(columns.at(aliases_index));
        }
        entry.description = takeNamedAliasDescription(entry.alternateNames);
        entry.alternateNames = uniqueNormalizedNames(entry.alternateNames, entry.objectName);

        if (type_codes_index >= 0)
        {
            const QString type_codes = trimQuoted(columns.at(type_codes_index)).trimmed();
            if (!type_codes.isEmpty())
            {
                QString formatted_type_codes = type_codes;
                formatted_type_codes.replace(QLatin1Char('|'), QStringLiteral(", "));
                entry.description = joinDescriptionAndNotes(
                    entry.description, {QCoreApplication::translate("CatalogSearchService", "SIMBAD type codes: %1")
                                            .arg(formatted_type_codes)});
            }
        }
        entries.push_back(entry);
    }

    if (!header_columns.isEmpty())
    {
        auto result = an::ui::CatalogSearchResult{};
        result.success = true;
        result.entries = std::move(entries);
        return result;
    }

    if (payload.trimmed().isEmpty())
    {
        an::ui::CatalogSearchResult result;
        result.success = true;
        return result;
    }

    return makeErrorResult(QStringLiteral("SIMBAD returned an unexpected response."));
}

[[nodiscard]] an::ui::CatalogSearchResult parseSesameResponse(const QByteArray& payload)
{
    if (payload.trimmed().isEmpty())
    {
        an::ui::CatalogSearchResult result;
        result.success = true;
        return result;
    }

    QXmlStreamReader reader(payload);
    QVector<an::ui::CatalogSearchEntry> entries;

    while (!reader.atEnd())
    {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QLatin1String("Resolver"))
        {
            continue;
        }

        an::ui::CatalogSearchEntry entry;
        entry.resolver = reader.attributes().value(QStringLiteral("name")).toString().trimmed();
        QString position;
        QString ra_value;
        QString dec_value;
        QString morphology_type;
        QString reference_position;
        QStringList aliases;

        while (!(reader.isEndElement() && reader.name() == QLatin1String("Resolver")) && !reader.atEnd())
        {
            reader.readNext();
            if (!reader.isStartElement())
            {
                continue;
            }

            if (reader.name() == QLatin1String("jpos"))
            {
                position = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            }
            else if (reader.name() == QLatin1String("jradeg"))
            {
                ra_value = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            }
            else if (reader.name() == QLatin1String("jdedeg"))
            {
                dec_value = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            }
            else if (reader.name() == QLatin1String("oname"))
            {
                entry.objectName = normalizedObjectName(reader.readElementText(QXmlStreamReader::SkipChildElements));
            }
            else if (reader.name() == QLatin1String("otype"))
            {
                entry.objectType = trimQuoted(reader.readElementText(QXmlStreamReader::SkipChildElements));
            }
            else if (reader.name() == QLatin1String("alias"))
            {
                aliases.push_back(reader.readElementText(QXmlStreamReader::SkipChildElements));
            }
            else if (reader.name() == QLatin1String("MType"))
            {
                morphology_type = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            }
            else if (reader.name() == QLatin1String("refPos"))
            {
                reference_position = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            }
        }

        double ra_deg = 0.0;
        double dec_deg = 0.0;
        bool has_coordinates = false;

        if (!ra_value.isEmpty() && !dec_value.isEmpty())
        {
            has_coordinates = parseDoubleValue(ra_value, ra_deg) && parseDoubleValue(dec_value, dec_deg);
            if (!has_coordinates)
            {
                return makeErrorResult(QStringLiteral("Sesame returned invalid coordinates."));
            }
        }
        else if (!position.isEmpty())
        {
            has_coordinates = parseCoordinatePair(position, ra_deg, dec_deg);
            if (!has_coordinates)
            {
                return makeErrorResult(QStringLiteral("Sesame returned invalid coordinates."));
            }
        }

        if (has_coordinates)
        {
            entry.raDeg = ra_deg;
            entry.decDeg = dec_deg;
            if (entry.objectName.isEmpty())
            {
                entry.objectName = QStringLiteral("Resolved object");
            }

            entry.alternateNames = uniqueNormalizedNames(aliases, entry.objectName);
            const QString alias_description = takeNamedAliasDescription(entry.alternateNames);
            QStringList detail_lines;
            if (!morphology_type.isEmpty())
            {
                detail_lines.push_back(
                    QCoreApplication::translate("CatalogSearchService", "Morphology: %1").arg(morphology_type));
            }
            if (!reference_position.isEmpty())
            {
                detail_lines.push_back(QCoreApplication::translate("CatalogSearchService", "Reference position: %1")
                                           .arg(reference_position));
            }
            entry.description = joinDescriptionAndNotes(alias_description, detail_lines);
            entries.push_back(entry);
        }
    }

    if (reader.hasError())
    {
        return makeErrorResult(QStringLiteral("Sesame returned malformed XML."));
    }

    an::ui::CatalogSearchResult result;
    result.success = true;
    result.entries = std::move(entries);
    return result;
}

} // namespace

namespace an::ui
{
namespace catalog_search
{

QString displayName(const CatalogServer server)
{
    switch (server)
    {
    case CatalogServer::Simbad:
        return QStringLiteral("SIMBAD");
    case CatalogServer::CdsSesame:
        return QStringLiteral("CDS Sesame");
    case CatalogServer::VizierSesame:
        return QStringLiteral("VizieR Sesame");
    case CatalogServer::LocalCatalog:
        return QStringLiteral("Local Catalog");
    }

    return QStringLiteral("Unknown server");
}

QUrl buildRequestUrl(const CatalogServer server, const QString& objectName)
{
    switch (server)
    {
    case CatalogServer::Simbad:
        return QUrl(QString::fromLatin1(kSimbadTapUrl));
    case CatalogServer::CdsSesame:
        return QUrl(QString::fromLatin1(kCdsSesameUrl) +
                    QString::fromLatin1(QUrl::toPercentEncoding(objectName.trimmed())));
    case CatalogServer::VizierSesame:
        return QUrl(QString::fromLatin1(kVizierSesameUrl) +
                    QString::fromLatin1(QUrl::toPercentEncoding(objectName.trimmed())));
    case CatalogServer::LocalCatalog:
        return {};
    }

    return {};
}

QByteArray buildRequestBody(const CatalogServer server, const QString& objectName)
{
    if (server != CatalogServer::Simbad)
    {
        return {};
    }

    const QString adql =
        QStringLiteral("SELECT basic.oid, basic.ra, basic.dec, basic.main_id, otypedef.otype_longname, "
                       "basic.otype AS type_codes, array_agg(ident_alias.id) AS aliases "
                       "FROM ident "
                       "JOIN basic ON ident.oidref = basic.oid "
                       "JOIN otypedef ON basic.otype = otypedef.otype "
                       "LEFT JOIN ident AS ident_alias ON ident_alias.oidref = basic.oid "
                       "WHERE ident.id = '%1' "
                       "GROUP BY basic.oid, basic.ra, basic.dec, basic.main_id, otypedef.otype_longname, basic.otype")
            .arg(adqlEscapedObjectName(objectName));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("request"), QStringLiteral("doQuery"));
    query.addQueryItem(QStringLiteral("lang"), QStringLiteral("adql"));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("tsv"));
    query.addQueryItem(QStringLiteral("query"), adql);
    return query.query(QUrl::FullyEncoded).toUtf8();
}

CatalogSearchResult parseResponse(const CatalogServer server, const QByteArray& payload)
{
    switch (server)
    {
    case CatalogServer::Simbad:
        return parseSimbadResponse(payload);
    case CatalogServer::CdsSesame:
    case CatalogServer::VizierSesame:
        return parseSesameResponse(payload);
    case CatalogServer::LocalCatalog:
        return makeErrorResult(QStringLiteral("Local catalog does not use HTTP responses."));
    }

    return makeErrorResult(QStringLiteral("Unsupported catalog server."));
}

LocalCatalogs loadLocalCatalogs(const an::consumer::EngineSession& session, const std::stop_token& stop,
                                const BundledCatalogSource& source)
{
    LocalCatalogs catalogs;
    an::Result<an::SkyCatalog> deepSky = loadBundledCatalog(session, BundledCatalog::DeepSky, stop, source);
    if (deepSky.hasValue())
    {
        catalogs.deepSky = std::move(*deepSky);
    }
    else
    {
        catalogs.deepSkyError = QString::fromUtf8(deepSky.status().message());
    }
    an::Result<an::SkyCatalog> brightStars = loadBundledCatalog(session, BundledCatalog::BrightStars, stop, source);
    if (brightStars.hasValue())
    {
        catalogs.brightStars = std::move(*brightStars);
    }
    else
    {
        catalogs.brightStarsError = QString::fromUtf8(brightStars.status().message());
    }
    LOG_INFO("Bundled local object catalogs ready: deepSkyLoaded={} brightStarLoaded={}", catalogs.deepSky.valid(),
             catalogs.brightStars.valid());
    return catalogs;
}

CatalogSearchResult searchLocalCatalogs(const an::ExecutionContext& execution, const LocalCatalogs& catalogs,
                                        const QString& objectName)
{
    CatalogSearchResult result;
    result.success = true;
    const std::string name = objectName.simplified().toStdString();

    // The deep-sky objects first, a typed designation read as its catalog spells it ("m83" as "M 83").
    for (const an::CatalogEntry* object : entriesNamed(execution, catalogs.deepSky, name, true))
    {
        CatalogSearchEntry entry;
        entry.objectName = QString::fromStdString(object->identifier);
        entry.objectType = QString::fromStdString(object->objectType);
        entry.raDeg = object->coordinate.rightAscensionDegrees;
        entry.decDeg = object->coordinate.declinationDegrees;
        entry.resolver = QStringLiteral("Local Catalog");

        if (!object->commonName.empty() && object->commonName != object->identifier)
        {
            entry.alternateNames.push_back(QString::fromStdString(object->commonName));
        }

        QStringList detail_lines;
        if (object->magnitude.has_value())
        {
            detail_lines.push_back(QCoreApplication::translate("CatalogSearchService", "Magnitude: %1")
                                       .arg(*object->magnitude, 0, 'f', 1));
        }
        if (object->extent.has_value() && object->extent->majorAxisArcminutes > 0.0)
        {
            detail_lines.push_back(QCoreApplication::translate("CatalogSearchService", "Size: %1' × %2'")
                                       .arg(object->extent->majorAxisArcminutes, 0, 'f', 1)
                                       .arg(object->extent->minorAxisArcminutes, 0, 'f', 1));
        }
        entry.description = detail_lines.join(QLatin1Char('\n'));

        result.entries.push_back(std::move(entry));
    }

    // Then the bright stars, by name as written.
    for (const an::CatalogEntry* star : entriesNamed(execution, catalogs.brightStars, name, false))
    {
        CatalogSearchEntry entry;
        entry.objectName = QString::fromStdString(star->commonName.empty() ? star->identifier : star->commonName);
        entry.objectType = QCoreApplication::translate("CatalogSearchService", "Star");
        entry.raDeg = star->coordinate.rightAscensionDegrees;
        entry.decDeg = star->coordinate.declinationDegrees;
        entry.resolver = QStringLiteral("Local Catalog");

        QStringList alternates;
        if (!star->commonName.empty())
        {
            alternates.push_back(QString::fromStdString(star->identifier));
        }
        if (!star->designation.empty())
        {
            alternates.push_back(QString::fromStdString(star->designation));
        }
        entry.alternateNames = alternates;

        QStringList detail_lines;
        if (star->magnitude.has_value())
        {
            detail_lines.push_back(
                QCoreApplication::translate("CatalogSearchService", "Magnitude: %1").arg(*star->magnitude, 0, 'f', 2));
        }
        if (!star->spectralType.empty())
        {
            detail_lines.push_back(QCoreApplication::translate("CatalogSearchService", "Spectral type: %1")
                                       .arg(QString::fromStdString(star->spectralType)));
        }
        entry.description = detail_lines.join(QLatin1Char('\n'));

        result.entries.push_back(std::move(entry));
    }

    if (!catalogs.deepSky.valid() && !catalogs.brightStars.valid())
    {
        QStringList errors;
        if (!catalogs.deepSkyError.isEmpty())
        {
            errors.push_back(catalogs.deepSkyError);
        }
        if (!catalogs.brightStarsError.isEmpty())
        {
            errors.push_back(catalogs.brightStarsError);
        }
        result.success = false;
        result.errorMessage = errors.isEmpty() ? QStringLiteral("The bundled catalogs could not be read.")
                                               : errors.join(QLatin1Char('\n'));
    }

    return result;
}

CatalogSearchResult searchLocalCatalog(const an::consumer::EngineSession& session, const QString& objectName,
                                       const std::stop_token& stop)
{
    const LocalCatalogs catalogs = loadLocalCatalogs(session, stop);
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview, .stopToken = stop});
    if (!scope.hasValue())
    {
        return makeErrorResult(QString::fromUtf8(scope.status().message()));
    }
    return searchLocalCatalogs(scope->execution(), catalogs, objectName);
}

QStringList updatedRecentHistory(QStringList existingHistory, const QString& acceptedObjectName, const int maxEntries)
{
    QStringList normalized;
    normalized.reserve(existingHistory.size() + 1);

    const auto appendUnique = [&normalized](const QString& value) {
        const QString trimmed = value.simplified();
        if (trimmed.isEmpty())
        {
            return;
        }

        const auto duplicate =
            std::find_if(normalized.cbegin(), normalized.cend(), [&trimmed](const QString& existing) {
                return existing.compare(trimmed, Qt::CaseInsensitive) == 0;
            });
        if (duplicate == normalized.cend())
        {
            normalized.push_back(trimmed);
        }
    };

    const QString accepted = acceptedObjectName.simplified();
    if (!accepted.isEmpty())
    {
        appendUnique(accepted);
    }

    for (const QString& existing : existingHistory)
    {
        appendUnique(existing);
        if (normalized.size() >= (std::max)(maxEntries, 0))
        {
            break;
        }
    }

    while (normalized.size() > (std::max)(maxEntries, 0))
    {
        normalized.removeLast();
    }

    return normalized;
}

} // namespace catalog_search

/// The bundled catalogs a service's local searches read, loaded by the first of them on a worker and kept for the
/// service's life; a search still running keeps them alive until it ends.
struct LocalSearchState
{
    std::mutex mutex;
    std::optional<catalog_search::LocalCatalogs> catalogs;

    /// Where the catalogs' text comes from; empty reads the product's resources.
    BundledCatalogSource source;
};

struct CatalogSearchService::impl
{
    QNetworkAccessManager networkManager;
    QPointer<QNetworkReply> activeReply;
    QTimer timeoutTimer;
    QString activeObjectName;
    CatalogServer activeServer = CatalogServer::Simbad;
    bool timedOut = false;
    bool suppressAbortError = false;

    std::shared_ptr<LocalSearchState> local = std::make_shared<LocalSearchState>();

    /// Moves for every search and every cancellation, so a local search superseded or cancelled never reports.
    std::uint64_t localGeneration = 0u;

    /// Stops the local search running now.
    std::shared_ptr<std::stop_source> localStop;
};

W_OBJECT_IMPL(CatalogSearchService)

CatalogSearchService::CatalogSearchService(QObject* parent) : QObject(parent), m_impl(std::make_unique<impl>())
{
    m_impl->networkManager.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    qRegisterMetaType<an::ui::CatalogSearchEntry>("an::ui::CatalogSearchEntry");
    qRegisterMetaType<an::ui::CatalogSearchResult>("an::ui::CatalogSearchResult");

    m_impl->timeoutTimer.setSingleShot(true);
    connect(&m_impl->timeoutTimer, &QTimer::timeout, this, [this]() {
        if (!m_impl->activeReply)
        {
            return;
        }

        m_impl->timedOut = true;
        LOG_WARNING("Catalog search timed out for '{}' via {}", m_impl->activeObjectName.toStdString(),
                    catalog_search::displayName(m_impl->activeServer).toStdString());
        m_impl->activeReply->abort();
    });
}

CatalogSearchService::~CatalogSearchService()
{
    cancel();
}

void CatalogSearchService::searchByName(const QString& objectName, const CatalogServer server)
{
    const QString trimmed_name = objectName.simplified();
    if (trimmed_name.isEmpty())
    {
        CatalogSearchResult result;
        result.success = false;
        result.errorMessage = tr("Enter an object name to search.");
        emit searchFinished(result);
        return;
    }

    cancel();

    m_impl->activeObjectName = trimmed_name;
    m_impl->activeServer = server;
    m_impl->timedOut = false;
    m_impl->suppressAbortError = false;

    if (server == CatalogServer::LocalCatalog)
    {
        LOG_INFO("Catalog search (local): object='{}'", trimmed_name.toStdString());
        const an::consumer::EngineSession* session = currentSession();
        if (session == nullptr)
        {
            emit searchFinished(makeErrorResult(tr("No engine session is available for the local catalog search.")));
            return;
        }
        // The bundled catalogs are read and searched on a worker; only the search asked for last reports.
        const std::uint64_t generation = ++m_impl->localGeneration;
        auto stop = std::make_shared<std::stop_source>();
        m_impl->localStop = stop;
        QPointer<CatalogSearchService> guard(this);
        QThreadPool::globalInstance()->start([guard, generation, session, local = m_impl->local, stop, trimmed_name]() {
            CatalogSearchResult result;
            {
                std::lock_guard lock(local->mutex);
                std::optional<catalog_search::LocalCatalogs> unkept;
                const catalog_search::LocalCatalogs* catalogs =
                    local->catalogs.has_value() ? &*local->catalogs : nullptr;
                if (catalogs == nullptr)
                {
                    catalog_search::LocalCatalogs loaded =
                        catalog_search::loadLocalCatalogs(*session, stop->get_token(), local->source);
                    if (stop->stop_requested())
                    {
                        // A stopped search never reports, and a load it cut short is not kept.
                        return;
                    }
                    // Catalogs that both read are kept; one that could not be read is read again next time.
                    if (loaded.deepSky.valid() && loaded.brightStars.valid())
                    {
                        local->catalogs = std::move(loaded);
                        catalogs = &*local->catalogs;
                    }
                    else
                    {
                        unkept = std::move(loaded);
                        catalogs = &*unkept;
                    }
                }
                an::Result<an::consumer::OperationScope> scope = session->admit(an::consumer::OperationRequest{
                    .purpose = an::consumer::OperationPurpose::Preview, .stopToken = stop->get_token()});
                result = scope.hasValue()
                             ? catalog_search::searchLocalCatalogs(scope->execution(), *catalogs, trimmed_name)
                             : makeErrorResult(QString::fromUtf8(scope.status().message()));
            }
            QCoreApplication* application = QCoreApplication::instance();
            if (application == nullptr)
            {
                return;
            }
            QMetaObject::invokeMethod(
                application,
                [guard, generation, result = std::move(result), trimmed_name]() {
                    if (!guard || guard->m_impl->localGeneration != generation)
                    {
                        return;
                    }
                    if (result.success)
                    {
                        LOG_INFO("Local catalog search finished: object='{}' results={}", trimmed_name.toStdString(),
                                 result.entries.size());
                    }
                    emit guard->searchFinished(result);
                },
                Qt::QueuedConnection);
        });
        return;
    }

    const QUrl request_url = catalog_search::buildRequestUrl(server, trimmed_name);
    if (request_url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 && !QSslSocket::supportsSsl())
    {
        const QString diagnostics = tlsDiagnosticsSummary();
        LOG_ERROR("Catalog search TLS unavailable before request: server={} object='{}' url={} diagnostics={}",
                  catalog_search::displayName(server).toStdString(), trimmed_name.toStdString(),
                  request_url.toString().toStdString(), diagnostics.toStdString());
        emit searchFinished(makeErrorResult(tlsUnavailableUserMessage()));
        return;
    }

    QNetworkRequest request(request_url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgentValue());
    request.setRawHeader("Accept", "text/tab-separated-values, application/xml, text/xml;q=0.9, */*;q=0.8");

    if (server == CatalogServer::Simbad)
    {
        const QByteArray body = catalog_search::buildRequestBody(server, trimmed_name);
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/x-www-form-urlencoded; charset=UTF-8"));
        LOG_INFO("Catalog search request: server={} object='{}' url={} body={} ",
                 catalog_search::displayName(server).toStdString(), trimmed_name.toStdString(),
                 request_url.toString().toStdString(), QString::fromUtf8(body).toStdString());
        m_impl->activeReply = m_impl->networkManager.post(request, body);
    }
    else
    {
        LOG_INFO("Catalog search request: server={} object='{}' url={}",
                 catalog_search::displayName(server).toStdString(), trimmed_name.toStdString(),
                 request_url.toString().toStdString());
        m_impl->activeReply = m_impl->networkManager.get(request);
    }

    connect(m_impl->activeReply.data(), &QNetworkReply::finished, this, &CatalogSearchService::handleReplyFinished);
    m_impl->timeoutTimer.start(kCatalogSearchTimeoutMs);
}

void CatalogSearchService::setLocalCatalogSource(BundledCatalogSource source)
{
    // The next local search reads the catalogs again; a search still running keeps the state it holds.
    auto replaced = std::make_shared<LocalSearchState>();
    replaced->source = std::move(source);
    m_impl->local = std::move(replaced);
}

void CatalogSearchService::cancel()
{
    ++m_impl->localGeneration;
    if (m_impl->localStop)
    {
        m_impl->localStop->request_stop();
        m_impl->localStop.reset();
    }
    m_impl->timeoutTimer.stop();
    m_impl->timedOut = false;

    if (!m_impl->activeReply)
    {
        return;
    }

    m_impl->suppressAbortError = true;
    QPointer<QNetworkReply> reply = m_impl->activeReply;
    m_impl->activeReply.clear();
    reply->abort();
    reply->deleteLater();
}

void CatalogSearchService::handleReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply == nullptr)
    {
        return;
    }

    const bool is_active_reply = (m_impl->activeReply == reply);
    if (!is_active_reply)
    {
        reply->deleteLater();
        return;
    }

    m_impl->timeoutTimer.stop();
    m_impl->activeReply.clear();

    const bool timed_out = m_impl->timedOut;
    const bool suppress_abort_error = m_impl->suppressAbortError;
    m_impl->timedOut = false;
    m_impl->suppressAbortError = false;

    const QByteArray payload = reply->isReadable() ? reply->readAll() : QByteArray{};
    const auto network_error = reply->error();
    const QVariant status_variant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int http_status = status_variant.isValid() ? status_variant.toInt() : 0;

    if (network_error != QNetworkReply::NoError)
    {
        if (network_error == QNetworkReply::OperationCanceledError && suppress_abort_error)
        {
            LOG_INFO("Catalog search cancelled for '{}'", m_impl->activeObjectName.toStdString());
            reply->deleteLater();
            return;
        }

        CatalogSearchResult result;
        result.success = false;
        result.errorMessage =
            timed_out
                ? tr("Catalog search timed out after %1 seconds.").arg(QString::number(kCatalogSearchTimeoutMs / 1000))
                : tr("Catalog search failed: %1").arg(reply->errorString());
        if (network_error == QNetworkReply::SslHandshakeFailedError ||
            reply->errorString().contains(QStringLiteral("TLS"), Qt::CaseInsensitive) ||
            reply->errorString().contains(QStringLiteral("SSL"), Qt::CaseInsensitive))
        {
            result.errorMessage = tlsUnavailableUserMessage();
        }
        LOG_ERROR("Catalog search network failure: server={} object='{}' status={} error={} payload_bytes={}",
                  catalog_search::displayName(m_impl->activeServer).toStdString(),
                  m_impl->activeObjectName.toStdString(), http_status, reply->errorString().toStdString(),
                  payload.size());
        if (network_error == QNetworkReply::SslHandshakeFailedError ||
            reply->errorString().contains(QStringLiteral("TLS"), Qt::CaseInsensitive) ||
            reply->errorString().contains(QStringLiteral("SSL"), Qt::CaseInsensitive))
        {
            LOG_ERROR("Catalog search TLS diagnostics: {}", tlsDiagnosticsSummary().toStdString());
        }
        emit searchFinished(result);
        reply->deleteLater();
        return;
    }

    if (http_status != 0 && (http_status < 200 || http_status >= 300))
    {
        CatalogSearchResult result;
        result.success = false;
        result.errorMessage = tr("Catalog search failed with HTTP status %1.").arg(QString::number(http_status));
        LOG_ERROR("Catalog search HTTP error: server={} object='{}' status={} payload='{}'",
                  catalog_search::displayName(m_impl->activeServer).toStdString(),
                  m_impl->activeObjectName.toStdString(), http_status,
                  QString::fromUtf8(payload.left(512)).toStdString());
        emit searchFinished(result);
        reply->deleteLater();
        return;
    }

    CatalogSearchResult result = catalog_search::parseResponse(m_impl->activeServer, payload);
    if (result.success)
    {
        for (CatalogSearchEntry& entry : result.entries)
        {
            if (entry.resolver.trimmed().isEmpty())
            {
                entry.resolver = catalog_search::displayName(m_impl->activeServer);
            }
        }
        LOG_INFO("Catalog search finished: server={} object='{}' status={} results={}",
                 catalog_search::displayName(m_impl->activeServer).toStdString(),
                 m_impl->activeObjectName.toStdString(), http_status, result.entries.size());
    }
    else
    {
        LOG_ERROR("Catalog search parse failure: server={} object='{}' status={} error={} payload='{}'",
                  catalog_search::displayName(m_impl->activeServer).toStdString(),
                  m_impl->activeObjectName.toStdString(), http_status, result.errorMessage.toStdString(),
                  QString::fromUtf8(payload.left(512)).toStdString());
    }

    emit searchFinished(result);
    reply->deleteLater();
}

} // namespace an::ui
