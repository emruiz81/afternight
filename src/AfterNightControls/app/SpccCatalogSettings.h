// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "app/CatalogDownloadService.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/IAppContext.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Image/SkyCoordinate.h>
#include <AfterNightPlatform/Sha256.h>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPair>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QVector>

#include "app/ApplicationStorage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace an::ui
{

inline constexpr auto SPCC_REGIONAL_CACHE_REQUEST_SCHEMA_VERSION = "spcc-regional-cache-request-v1";
inline constexpr auto SPCC_REGIONAL_CACHE_CATALOG_FAMILY_ID = "spcc_gaia_dr3_xp_sampled";
inline constexpr auto SPCC_REGIONAL_CACHE_ACCESS_PROFILE = "installed-regional-cache";
inline constexpr auto SPCC_LOCAL_FULL_ACCESS_PROFILE = "local-full";

namespace spcc_catalog_detail
{

[[nodiscard]] inline bool isSha256Hex(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.size() != 64)
    {
        return false;
    }
    for (const QChar ch : trimmed)
    {
        const ushort lower = ch.toLower().unicode();
        if (!ch.isDigit() && (lower < static_cast<ushort>('a') || lower > static_cast<ushort>('f')))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline QString sha256File(const QString& path)
{
    const std::optional<std::array<std::uint8_t, 32>> digest =
        an::platform::sha256OfFile(std::filesystem::path(path.toStdWString()));
    return digest.has_value() ? QString::fromStdString(an::platform::sha256Hex(*digest)) : QString();
}

[[nodiscard]] inline bool isSafeRelativePath(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('/')) || trimmed.startsWith(QStringLiteral("//")) ||
        trimmed.contains(QLatin1Char('\\')) || trimmed.contains(QLatin1Char('#')) || trimmed.contains(QLatin1Char('?')))
    {
        return false;
    }

    const QUrl parsed(trimmed);
    if (parsed.isValid() && (!parsed.scheme().isEmpty() || !parsed.host().isEmpty()))
    {
        return false;
    }

    const QString cleaned = QDir::cleanPath(trimmed);
    return cleaned != QStringLiteral(".") && cleaned != QStringLiteral("..") &&
           !cleaned.startsWith(QStringLiteral("../")) && !cleaned.contains(QStringLiteral("/../")) &&
           cleaned == trimmed;
}

[[nodiscard]] inline bool isSafePathSegment(const QString& value)
{
    const QString trimmed = value.trimmed();
    return isSafeRelativePath(trimmed) && !trimmed.contains(QLatin1Char('/')) && !trimmed.contains(QLatin1Char('\\'));
}

} // namespace spcc_catalog_detail

enum class SpccCatalogDirectoryStatus
{
    Missing,
    Ready,
    PartialCache,
    IncompleteInstall,
    Invalid,
};

struct SpccCatalogDirectoryInspection
{
    SpccCatalogDirectoryStatus status = SpccCatalogDirectoryStatus::Missing;
    QString directoryPath;
    QString catalogFamilyId;
    QString manifestId;
    QString accessProfile;
    QString rightsReviewStatus;
    QString diagnostic;
    bool hasInstallMarker = false;
    bool checksumsValid = true;
    int chunkCount = 0;
    int chunkFilesPresent = 0;
    int chunkFilesMissing = 0;
    int sidecarFilesPresent = 0;
    int declaredTileCount = 0;
    int expectedTileCount = 0;
    int expectedParentChunkCount = 0;
    qint64 chunkBytesPresent = 0;
    qint64 chunkBytesMissing = 0;
    qint64 chunkBytesTotal = 0;
    int healpixOrder = an::kXpCatalogHealpixOrder;
    int chunkParentHealpixOrder = an::kXpChunkParentHealpixOrder;
    QVector<QString> chunkFiles;
    QVector<qint64> chunkSizeBytes;
    QVector<bool> chunkFilePresent;
    QVector<QPair<int, int>> chunkTileRanges;
};

struct SpccCatalogRequiredChunkAvailability
{
    bool valid = false;
    QVector<int> requiredChunkIndices;
    QVector<int> cachedChunkIndices;
    QVector<int> missingChunkIndices;
    qint64 missingBytes = 0;
    QString diagnostic;

    [[nodiscard]] bool allRequiredChunksCached() const noexcept
    {
        return valid && missingChunkIndices.isEmpty();
    }
};

struct SpccCatalogPackLocation
{
    QString manifestId;
    QString directoryPath;
    QString displayName;
    SpccCatalogDirectoryInspection inspection;
};

struct SpccRegionalCatalogRequest
{
    QString schemaVersion = QStringLiteral("spcc-regional-cache-request-v1");
    QString catalogFamilyId = QStringLiteral("spcc_gaia_dr3_xp_sampled");
    QString accessProfile = QStringLiteral("installed-regional-cache");
    QString targetName;
    QString createdAtUtc;
    double centerRaDeg = 0.0;
    double centerDecDeg = 0.0;
    double radiusDeg = 0.0;
    double magnitudeLimit = 0.0;
    int imageWidth = 0;
    int imageHeight = 0;
};

[[nodiscard]] inline double spccCatalogDegreesToRadians(const double degrees)
{
    constexpr double kPi = 3.141592653589793238462643383279502884;
    return degrees * kPi / 180.0;
}

[[nodiscard]] inline double spccCatalogRadiansToDegrees(const double radians)
{
    constexpr double kPi = 3.141592653589793238462643383279502884;
    return radians * 180.0 / kPi;
}

[[nodiscard]] inline double spccCatalogNormalizeRaDeltaDeg(double deltaDeg)
{
    while (deltaDeg > 180.0)
    {
        deltaDeg -= 360.0;
    }
    while (deltaDeg < -180.0)
    {
        deltaDeg += 360.0;
    }
    return deltaDeg;
}

[[nodiscard]] inline double spccCatalogAngularSeparationDeg(const double ra1Deg, const double dec1Deg,
                                                            const double ra2Deg, const double dec2Deg)
{
    const double raDeltaRad = spccCatalogDegreesToRadians(spccCatalogNormalizeRaDeltaDeg(ra2Deg - ra1Deg));
    const double dec1Rad = spccCatalogDegreesToRadians(dec1Deg);
    const double dec2Rad = spccCatalogDegreesToRadians(dec2Deg);
    const double decDeltaRad = dec2Rad - dec1Rad;

    const double sinHalfDec = std::sin(decDeltaRad * 0.5);
    const double sinHalfRa = std::sin(raDeltaRad * 0.5);
    const double a = sinHalfDec * sinHalfDec + std::cos(dec1Rad) * std::cos(dec2Rad) * sinHalfRa * sinHalfRa;
    return spccCatalogRadiansToDegrees(2.0 * std::asin(std::sqrt((std::min)(1.0, (std::max)(0.0, a)))));
}

[[nodiscard]] inline bool isValidSpccRegionalCatalogRequest(const SpccRegionalCatalogRequest& request)
{
    return request.schemaVersion == QString::fromLatin1(SPCC_REGIONAL_CACHE_REQUEST_SCHEMA_VERSION) &&
           request.catalogFamilyId == QString::fromLatin1(SPCC_REGIONAL_CACHE_CATALOG_FAMILY_ID) &&
           request.accessProfile == QString::fromLatin1(SPCC_REGIONAL_CACHE_ACCESS_PROFILE) &&
           !request.createdAtUtc.trimmed().isEmpty() && std::isfinite(request.centerRaDeg) &&
           request.centerRaDeg >= 0.0 && request.centerRaDeg < 360.0 && std::isfinite(request.centerDecDeg) &&
           request.centerDecDeg >= -90.0 && request.centerDecDeg <= 90.0 && std::isfinite(request.radiusDeg) &&
           request.radiusDeg > 0.0 && std::isfinite(request.magnitudeLimit) && request.magnitudeLimit > 0.0 &&
           request.imageWidth > 0 && request.imageHeight > 0;
}

[[nodiscard]] inline bool isApprovedSpccCatalogDescriptorRightsStatus(const QString& rightsReviewStatus)
{
    const QString normalized = rightsReviewStatus.trimmed();
    return normalized.compare(QStringLiteral("approved-public"), Qt::CaseInsensitive) == 0 ||
           normalized.compare(QStringLiteral("approved-for-managed-download"), Qt::CaseInsensitive) == 0 ||
           normalized.compare(QStringLiteral("approved-for-bundling"), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] inline bool isSupportedSpccCatalogDescriptorAccessProfile(const QString& accessProfile)
{
    const QString normalized = accessProfile.trimmed();
    return normalized.compare(QString::fromLatin1(SPCC_REGIONAL_CACHE_ACCESS_PROFILE), Qt::CaseInsensitive) == 0 ||
           normalized.compare(QString::fromLatin1(SPCC_LOCAL_FULL_ACCESS_PROFILE), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] inline bool isDownloadableManagedSpccCatalogDescriptor(const CatalogPackDescriptor& descriptor)
{
    return !descriptor.id.trimmed().isEmpty() && spcc_catalog_detail::isSafePathSegment(descriptor.id) &&
           isUsableCatalogManifestUrl(descriptor.manifestUrl) &&
           descriptor.distributionType.trimmed().compare(QStringLiteral("chunks"), Qt::CaseInsensitive) == 0 &&
           descriptor.catalogFamilyId.trimmed().compare(QString::fromLatin1(SPCC_REGIONAL_CACHE_CATALOG_FAMILY_ID),
                                                        Qt::CaseInsensitive) == 0 &&
           isSupportedSpccCatalogDescriptorAccessProfile(descriptor.accessProfile) &&
           isApprovedSpccCatalogDescriptorRightsStatus(descriptor.rightsReviewStatus);
}

[[nodiscard]] inline bool isDownloadableManagedSpccLocalFullCatalogDescriptor(const CatalogPackDescriptor& descriptor)
{
    return isDownloadableManagedSpccCatalogDescriptor(descriptor) &&
           descriptor.accessProfile.trimmed().compare(QString::fromLatin1(SPCC_LOCAL_FULL_ACCESS_PROFILE),
                                                      Qt::CaseInsensitive) == 0;
}

[[nodiscard]] inline int spccCatalogHealpixPixelCountForOrder(const int order)
{
    if (order < 0 || order > 12)
    {
        return 0;
    }

    int scale = 1;
    for (int level = 0; level < order; ++level)
    {
        scale *= 4;
    }
    return 12 * scale;
}

[[nodiscard]] inline bool spccCatalogPackDescriptorCoversRegionalRequest(const CatalogPackDescriptor& descriptor,
                                                                         const SpccRegionalCatalogRequest& request,
                                                                         QString* rejectionReason = nullptr)
{
    auto reject = [rejectionReason](const QString& reason) {
        if (rejectionReason)
        {
            *rejectionReason = reason;
        }
        return false;
    };

    if (rejectionReason)
    {
        rejectionReason->clear();
    }

    if (!isValidSpccRegionalCatalogRequest(request))
    {
        return reject(QObject::tr("The saved SPCC regional-cache request is invalid."));
    }
    if (descriptor.id.trimmed().isEmpty() || !isUsableCatalogManifestUrl(descriptor.manifestUrl))
    {
        return reject(QObject::tr("The catalog descriptor does not provide a valid manifest URL."));
    }
    if (descriptor.distributionType.trimmed().compare(QStringLiteral("chunks"), Qt::CaseInsensitive) != 0)
    {
        return reject(QObject::tr("The catalog descriptor does not use the supported chunk distribution."));
    }
    if (descriptor.catalogFamilyId.trimmed().compare(request.catalogFamilyId, Qt::CaseInsensitive) != 0)
    {
        return reject(QObject::tr("The catalog descriptor is for a different SPCC catalog family."));
    }
    if (descriptor.accessProfile.trimmed().compare(request.accessProfile, Qt::CaseInsensitive) != 0)
    {
        return reject(QObject::tr("The catalog descriptor is not an installed regional-cache pack."));
    }
    if (!isApprovedSpccCatalogDescriptorRightsStatus(descriptor.rightsReviewStatus))
    {
        return reject(QObject::tr("The catalog descriptor is not rights-approved for download."));
    }
    if (!descriptor.hasRegionCoverage || !std::isfinite(descriptor.centerRaDeg) ||
        !std::isfinite(descriptor.centerDecDeg) || !std::isfinite(descriptor.coverageRadiusDeg) ||
        descriptor.centerRaDeg < 0.0 || descriptor.centerRaDeg >= 360.0 || descriptor.centerDecDeg < -90.0 ||
        descriptor.centerDecDeg > 90.0 || descriptor.coverageRadiusDeg <= 0.0)
    {
        return reject(QObject::tr("The catalog descriptor does not declare valid sky coverage."));
    }
    if (!std::isfinite(descriptor.magnitudeLimit) || descriptor.magnitudeLimit < request.magnitudeLimit)
    {
        return reject(QObject::tr("The catalog descriptor magnitude limit is too shallow for the request."));
    }

    const double separationDeg = spccCatalogAngularSeparationDeg(request.centerRaDeg, request.centerDecDeg,
                                                                 descriptor.centerRaDeg, descriptor.centerDecDeg);
    if (separationDeg + request.radiusDeg > descriptor.coverageRadiusDeg + 1.0e-9)
    {
        return reject(QObject::tr("The catalog descriptor sky coverage does not contain the requested region."));
    }

    return true;
}

[[nodiscard]] inline QVector<CatalogPackDescriptor>
spccRegionalCatalogPacksForRequest(const QVector<CatalogPackDescriptor>& descriptors,
                                   const SpccRegionalCatalogRequest& request)
{
    QVector<CatalogPackDescriptor> matches;
    for (const CatalogPackDescriptor& descriptor : descriptors)
    {
        if (spccCatalogPackDescriptorCoversRegionalRequest(descriptor, request))
        {
            matches.push_back(descriptor);
        }
    }

    std::stable_sort(matches.begin(), matches.end(),
                     [](const CatalogPackDescriptor& lhs, const CatalogPackDescriptor& rhs) {
                         if (lhs.recommended != rhs.recommended)
                         {
                             return lhs.recommended;
                         }
                         return lhs.coverageRadiusDeg < rhs.coverageRadiusDeg;
                     });
    return matches;
}

[[nodiscard]] inline QJsonObject spccRegionalCatalogRequestToJsonObject(const SpccRegionalCatalogRequest& request)
{
    return QJsonObject{
        {QStringLiteral("schema_version"), request.schemaVersion},
        {QStringLiteral("catalog_family_id"), request.catalogFamilyId},
        {QStringLiteral("access_profile"), request.accessProfile},
        {QStringLiteral("target_name"), request.targetName},
        {QStringLiteral("created_at_utc"), request.createdAtUtc},
        {QStringLiteral("region"),
         QJsonObject{
             {QStringLiteral("center_ra_deg"), request.centerRaDeg},
             {QStringLiteral("center_dec_deg"), request.centerDecDeg},
             {QStringLiteral("radius_deg"), request.radiusDeg},
         }},
        {QStringLiteral("selection"),
         QJsonObject{
             {QStringLiteral("magnitude_limit"), request.magnitudeLimit},
         }},
        {QStringLiteral("image"),
         QJsonObject{
             {QStringLiteral("width"), request.imageWidth},
             {QStringLiteral("height"), request.imageHeight},
         }},
    };
}

[[nodiscard]] inline QString serializeSpccRegionalCatalogRequest(const SpccRegionalCatalogRequest& request)
{
    if (!isValidSpccRegionalCatalogRequest(request))
    {
        return {};
    }

    return QString::fromUtf8(
        QJsonDocument(spccRegionalCatalogRequestToJsonObject(request)).toJson(QJsonDocument::Compact));
}

[[nodiscard]] inline std::optional<SpccRegionalCatalogRequest>
parseSpccRegionalCatalogRequest(const QString& serialized)
{
    const QByteArray bytes = serialized.trimmed().toUtf8();
    if (bytes.isEmpty())
    {
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    const QJsonObject region = object.value(QStringLiteral("region")).toObject();
    const QJsonObject selection = object.value(QStringLiteral("selection")).toObject();
    const QJsonObject image = object.value(QStringLiteral("image")).toObject();

    SpccRegionalCatalogRequest request;
    request.schemaVersion = object.value(QStringLiteral("schema_version")).toString();
    request.catalogFamilyId = object.value(QStringLiteral("catalog_family_id")).toString();
    request.accessProfile = object.value(QStringLiteral("access_profile")).toString();
    request.targetName = object.value(QStringLiteral("target_name")).toString();
    request.createdAtUtc = object.value(QStringLiteral("created_at_utc")).toString();
    request.centerRaDeg = region.value(QStringLiteral("center_ra_deg")).toDouble();
    request.centerDecDeg = region.value(QStringLiteral("center_dec_deg")).toDouble();
    request.radiusDeg = region.value(QStringLiteral("radius_deg")).toDouble();
    request.magnitudeLimit = selection.value(QStringLiteral("magnitude_limit")).toDouble();
    request.imageWidth = image.value(QStringLiteral("width")).toInt();
    request.imageHeight = image.value(QStringLiteral("height")).toInt();

    if (!isValidSpccRegionalCatalogRequest(request))
    {
        return std::nullopt;
    }
    return request;
}

/// The regional catalog request a plate-solved image asks for: the field's centre and the radius of a cone that holds
/// the whole field with the match radius beside it, both the solution's own.
[[nodiscard]] inline std::optional<SpccRegionalCatalogRequest>
makeSpccRegionalCatalogRequestFromWcs(const an::WcsSolution& solution, const double matchRadiusArcsec,
                                      const double magnitudeLimit, const QString& targetName,
                                      QString* diagnostic = nullptr)
{
    const an::Result<an::SkyCoordinate> center = solution.fieldCenter();
    const an::Result<double> radiusDeg = solution.queryRadiusDegrees(matchRadiusArcsec);
    if (!center.hasValue() || !radiusDeg.hasValue() || !std::isfinite(*radiusDeg) || *radiusDeg <= 0.0)
    {
        if (diagnostic)
        {
            *diagnostic = QObject::tr("SPCC regional catalog requests could not derive a valid WCS footprint.");
        }
        return std::nullopt;
    }

    SpccRegionalCatalogRequest request;
    request.targetName = targetName.trimmed();
    request.createdAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    request.centerRaDeg = center->rightAscensionDegrees;
    request.centerDecDeg = center->declinationDegrees;
    request.radiusDeg = *radiusDeg;
    request.magnitudeLimit = magnitudeLimit;
    request.imageWidth = static_cast<int>(solution.imageWidth());
    request.imageHeight = static_cast<int>(solution.imageHeight());

    if (!isValidSpccRegionalCatalogRequest(request))
    {
        if (diagnostic)
        {
            *diagnostic = QObject::tr("SPCC regional catalog request parameters are outside the supported range.");
        }
        return std::nullopt;
    }
    return request;
}

inline bool saveSpccRegionalCatalogRequest(IGlobalSettingsProvider* settings, const SpccRegionalCatalogRequest& request)
{
    if (!settings)
    {
        return false;
    }

    const QString serialized = serializeSpccRegionalCatalogRequest(request);
    return !serialized.isEmpty() && settings->upsert(SettingsKeys::CATALOGS_SPCC_REGIONAL_CACHE_REQUEST, serialized);
}

inline bool saveSpccRegionalCatalogRequest(GlobalSettingsPersistence* settings,
                                           const SpccRegionalCatalogRequest& request)
{
    if (!settings)
    {
        return false;
    }

    const QString serialized = serializeSpccRegionalCatalogRequest(request);
    return !serialized.isEmpty() && settings->upsert(SettingsKeys::CATALOGS_SPCC_REGIONAL_CACHE_REQUEST, serialized);
}

inline bool clearSpccRegionalCatalogRequest(IGlobalSettingsProvider* settings)
{
    return settings && settings->upsert(SettingsKeys::CATALOGS_SPCC_REGIONAL_CACHE_REQUEST, QString());
}

inline bool clearSpccRegionalCatalogRequest(GlobalSettingsPersistence* settings)
{
    return settings && settings->remove(SettingsKeys::CATALOGS_SPCC_REGIONAL_CACHE_REQUEST);
}

[[nodiscard]] inline std::optional<SpccRegionalCatalogRequest>
loadSpccRegionalCatalogRequest(IGlobalSettingsProvider* settings)
{
    if (!settings)
    {
        return std::nullopt;
    }

    const std::optional<QString> stored = settings->selectOptional(SettingsKeys::CATALOGS_SPCC_REGIONAL_CACHE_REQUEST);
    return stored.has_value() ? parseSpccRegionalCatalogRequest(*stored) : std::nullopt;
}

[[nodiscard]] inline std::optional<SpccRegionalCatalogRequest>
loadSpccRegionalCatalogRequest(GlobalSettingsPersistence* settings)
{
    if (!settings)
    {
        return std::nullopt;
    }

    const std::optional<QString> stored = settings->selectOptional(SettingsKeys::CATALOGS_SPCC_REGIONAL_CACHE_REQUEST);
    return stored.has_value() ? parseSpccRegionalCatalogRequest(*stored) : std::nullopt;
}

[[nodiscard]] inline QString defaultSpccCatalogMirrorBaseUrl()
{
    return QStringLiteral("https://huggingface.co/datasets/emruiz81/afternight-spcc-catalogs/resolve/main");
}

[[nodiscard]] inline QString normalizeCatalogMirrorBaseUrl(const QString& baseUrl)
{
    const QString trimmed = baseUrl.trimmed();
    return trimmed.endsWith(QLatin1Char('/')) ? trimmed.chopped(1) : trimmed;
}

[[nodiscard]] inline QString spccCatalogMirrorConfigFilePath()
{
    static const QString kFileName = QStringLiteral("spcc_catalog_mirrors.json");

    const QString dataDir = applicationLocalDataPath();
    return dataDir.isEmpty() ? QString() : QDir(dataDir).filePath(kFileName);
}

[[nodiscard]] inline QString spccCatalogMirrorBaseUrlFromConfig(const QJsonObject& config)
{
    const QJsonArray mirrors = config.value(QStringLiteral("mirrors")).toArray();
    for (const QJsonValue& mirrorValue : mirrors)
    {
        const QString url = normalizeCatalogMirrorBaseUrl(mirrorValue.toString());
        if (!url.isEmpty())
        {
            return url;
        }
    }

    return {};
}

[[nodiscard]] inline std::optional<QJsonObject> loadSpccCatalogMirrorConfigObject()
{
    const QString filePath = spccCatalogMirrorConfigFilePath();
    if (filePath.isEmpty())
    {
        return std::nullopt;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::nullopt;
    }

    return document.object();
}

[[nodiscard]] inline QString normalizeRelativeSpccCatalogManifestPath(const QString& manifestPath)
{
    const QString normalizedSeparators = QDir::fromNativeSeparators(manifestPath.trimmed());
    if (normalizedSeparators.isEmpty() || normalizedSeparators.startsWith(QLatin1Char('/')))
    {
        return {};
    }

    const QUrl asUrl(normalizedSeparators);
    if (asUrl.isValid() && !asUrl.scheme().isEmpty())
    {
        return {};
    }

    const QString cleaned = QDir::cleanPath(normalizedSeparators);
    if (cleaned.isEmpty() || cleaned == QLatin1String(".") || cleaned == QLatin1String("..") ||
        cleaned.startsWith(QStringLiteral("../")) || cleaned.contains(QStringLiteral("/../")) ||
        !cleaned.endsWith(QStringLiteral("catalog_meta.json")))
    {
        return {};
    }

    return cleaned;
}

[[nodiscard]] inline QUrl spccCatalogDescriptorManifestUrl(const QJsonObject& descriptorObject,
                                                           const QString& mirrorBaseUrl)
{
    const QString manifestUrl = descriptorObject.value(QStringLiteral("manifest_url")).toString().trimmed();
    if (!manifestUrl.isEmpty())
    {
        const QUrl url(manifestUrl);
        if (url.isValid() && !url.isRelative() && url.fragment().isEmpty())
        {
            return url;
        }
        return {};
    }

    const QString manifestPath =
        normalizeRelativeSpccCatalogManifestPath(descriptorObject.value(QStringLiteral("manifest_path")).toString());
    if (manifestPath.isEmpty())
    {
        return {};
    }

    const QString base = normalizeCatalogMirrorBaseUrl(mirrorBaseUrl);
    if (base.isEmpty())
    {
        return {};
    }

    const QUrl baseUrl(base + QLatin1Char('/'));
    const QUrl resolved = baseUrl.resolved(QUrl(manifestPath));
    return resolved.isValid() && !resolved.isRelative() ? resolved : QUrl();
}

[[nodiscard]] inline std::optional<CatalogPackDescriptor>
spccCatalogPackDescriptorFromMirrorConfigObject(const QJsonObject& descriptorObject, const QString& mirrorBaseUrl,
                                                QString* rejectionReason = nullptr)
{
    auto reject = [rejectionReason](const QString& reason) -> std::optional<CatalogPackDescriptor> {
        if (rejectionReason)
        {
            *rejectionReason = reason;
        }
        return std::nullopt;
    };

    if (rejectionReason)
    {
        rejectionReason->clear();
    }

    CatalogPackDescriptor descriptor;
    descriptor.id = descriptorObject.value(QStringLiteral("id")).toString().trimmed();
    descriptor.displayName = descriptorObject.value(QStringLiteral("display_name")).toString().trimmed();
    descriptor.description = descriptorObject.value(QStringLiteral("description")).toString().trimmed();
    descriptor.manifestUrl = spccCatalogDescriptorManifestUrl(descriptorObject, mirrorBaseUrl);
    descriptor.catalogMetaSha256 = descriptorObject.value(QStringLiteral("catalog_meta_sha256")).toString().trimmed();
    descriptor.distributionType = descriptorObject.value(QStringLiteral("distribution_type")).toString().trimmed();
    descriptor.catalogFamilyId = descriptorObject.value(QStringLiteral("catalog_family_id")).toString().trimmed();
    descriptor.accessProfile = descriptorObject.value(QStringLiteral("access_profile")).toString().trimmed();
    descriptor.rightsReviewStatus = descriptorObject.value(QStringLiteral("rights_review_status")).toString().trimmed();
    if (descriptor.rightsReviewStatus.isEmpty())
    {
        descriptor.rightsReviewStatus = descriptorObject.value(QStringLiteral("rights"))
                                            .toObject()
                                            .value(QStringLiteral("review_status"))
                                            .toString()
                                            .trimmed();
    }
    descriptor.releaseStatus =
        descriptorObject.value(QStringLiteral("release_status")).toString(QStringLiteral("current")).trimmed();
    descriptor.recommended = descriptorObject.value(QStringLiteral("recommended")).toBool(false);
    descriptor.downloadSizeBytes =
        descriptorObject.value(QStringLiteral("download_size_bytes")).toVariant().toLongLong();
    descriptor.installedSizeBytes =
        descriptorObject.value(QStringLiteral("installed_size_bytes")).toVariant().toLongLong();

    if (descriptor.id.isEmpty())
    {
        return reject(QObject::tr("The SPCC catalog descriptor is missing an ID."));
    }
    if (!spcc_catalog_detail::isSafePathSegment(descriptor.id))
    {
        return reject(QObject::tr("The SPCC catalog descriptor has an unsafe ID."));
    }
    if (descriptor.displayName.isEmpty())
    {
        descriptor.displayName = descriptor.id;
    }
    if (!isUsableCatalogManifestUrl(descriptor.manifestUrl))
    {
        return reject(QObject::tr("The SPCC catalog descriptor has an invalid manifest URL."));
    }
    if (descriptor.catalogMetaSha256.isEmpty())
    {
        return reject(QObject::tr("The SPCC catalog descriptor is missing its manifest checksum."));
    }
    if (!spcc_catalog_detail::isSha256Hex(descriptor.catalogMetaSha256))
    {
        return reject(QObject::tr("The SPCC catalog descriptor has an invalid manifest checksum."));
    }
    if (descriptor.distributionType.compare(QStringLiteral("chunks"), Qt::CaseInsensitive) != 0)
    {
        return reject(QObject::tr("The SPCC catalog descriptor does not use chunk distribution."));
    }
    if (descriptor.catalogFamilyId.compare(QString::fromLatin1(SPCC_REGIONAL_CACHE_CATALOG_FAMILY_ID),
                                           Qt::CaseInsensitive) != 0)
    {
        return reject(QObject::tr("The SPCC catalog descriptor uses an unsupported catalog family."));
    }
    if (!isSupportedSpccCatalogDescriptorAccessProfile(descriptor.accessProfile))
    {
        return reject(QObject::tr("The SPCC catalog descriptor uses an unsupported access profile."));
    }
    if (!isApprovedSpccCatalogDescriptorRightsStatus(descriptor.rightsReviewStatus))
    {
        return reject(QObject::tr("The SPCC catalog descriptor is not rights-approved for managed download."));
    }
    if (descriptor.downloadSizeBytes < 0 || descriptor.installedSizeBytes < 0)
    {
        return reject(QObject::tr("The SPCC catalog descriptor has an invalid byte size."));
    }
    if (descriptor.releaseStatus.compare(QStringLiteral("current"), Qt::CaseInsensitive) != 0)
    {
        return reject(QObject::tr("The SPCC catalog descriptor is not current."));
    }

    const QJsonObject coverage = descriptorObject.value(QStringLiteral("coverage")).toObject();
    if (!coverage.isEmpty())
    {
        descriptor.centerRaDeg = coverage.value(QStringLiteral("center_ra_deg")).toDouble();
        descriptor.centerDecDeg = coverage.value(QStringLiteral("center_dec_deg")).toDouble();
        descriptor.coverageRadiusDeg = coverage.value(QStringLiteral("radius_deg")).toDouble();
        descriptor.magnitudeLimit = coverage.value(QStringLiteral("magnitude_limit")).toDouble();
        descriptor.hasRegionCoverage = std::isfinite(descriptor.centerRaDeg) && descriptor.centerRaDeg >= 0.0 &&
                                       descriptor.centerRaDeg < 360.0 && std::isfinite(descriptor.centerDecDeg) &&
                                       descriptor.centerDecDeg >= -90.0 && descriptor.centerDecDeg <= 90.0 &&
                                       std::isfinite(descriptor.coverageRadiusDeg) &&
                                       descriptor.coverageRadiusDeg > 0.0 && std::isfinite(descriptor.magnitudeLimit) &&
                                       descriptor.magnitudeLimit > 0.0;
        if (!descriptor.hasRegionCoverage)
        {
            return reject(QObject::tr("The SPCC catalog descriptor has invalid sky coverage."));
        }
    }
    if (descriptor.accessProfile.compare(QString::fromLatin1(SPCC_REGIONAL_CACHE_ACCESS_PROFILE),
                                         Qt::CaseInsensitive) == 0 &&
        !descriptor.hasRegionCoverage)
    {
        return reject(QObject::tr("The SPCC regional-cache descriptor is missing sky coverage."));
    }

    return descriptor;
}

[[nodiscard]] inline QVector<CatalogPackDescriptor>
availableSpccCatalogPacksForMirrorConfig(const QJsonObject& config, QString* diagnostic = nullptr)
{
    if (diagnostic)
    {
        diagnostic->clear();
    }

    const QString mirrorBaseUrl = spccCatalogMirrorBaseUrlFromConfig(config);
    const QJsonArray descriptorArray = config.value(QStringLiteral("catalog_packs")).toArray();
    QVector<CatalogPackDescriptor> packs;
    int rejectedCount = 0;
    QString lastRejectionReason;
    for (const QJsonValue& descriptorValue : descriptorArray)
    {
        if (!descriptorValue.isObject())
        {
            ++rejectedCount;
            lastRejectionReason = QObject::tr("A SPCC catalog descriptor entry is not a JSON object.");
            continue;
        }

        QString rejectionReason;
        const std::optional<CatalogPackDescriptor> descriptor = spccCatalogPackDescriptorFromMirrorConfigObject(
            descriptorValue.toObject(), mirrorBaseUrl, &rejectionReason);
        if (!descriptor.has_value())
        {
            ++rejectedCount;
            lastRejectionReason = rejectionReason;
            continue;
        }
        packs.push_back(*descriptor);
    }

    std::stable_sort(packs.begin(), packs.end(),
                     [](const CatalogPackDescriptor& lhs, const CatalogPackDescriptor& rhs) {
                         if (lhs.recommended != rhs.recommended)
                         {
                             return lhs.recommended;
                         }
                         return lhs.displayName.compare(rhs.displayName, Qt::CaseInsensitive) < 0;
                     });

    if (diagnostic && rejectedCount > 0)
    {
        *diagnostic =
            QObject::tr("%1 SPCC catalog descriptor(s) were ignored. %2").arg(rejectedCount).arg(lastRejectionReason);
    }
    return packs;
}

[[nodiscard]] inline QVector<CatalogPackDescriptor>
availableSpccCatalogPacksForMirrorConfigJson(const QByteArray& configBytes, QString* diagnostic = nullptr)
{
    if (diagnostic)
    {
        diagnostic->clear();
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(configBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (diagnostic)
        {
            *diagnostic = QObject::tr("The SPCC catalog mirror config is not valid JSON.");
        }
        return {};
    }

    return availableSpccCatalogPacksForMirrorConfig(document.object(), diagnostic);
}

[[nodiscard]] inline QString loadSpccCatalogMirrorBaseUrl()
{
    const std::optional<QJsonObject> config = loadSpccCatalogMirrorConfigObject();
    if (config.has_value())
    {
        const QString baseUrl = spccCatalogMirrorBaseUrlFromConfig(*config);
        if (!baseUrl.isEmpty())
        {
            return baseUrl;
        }
    }

    return defaultSpccCatalogMirrorBaseUrl();
}

[[nodiscard]] inline QVector<CatalogPackDescriptor> availableSpccCatalogPacksForBaseUrl(const QString& baseUrl)
{
    const QString normalizedBase = normalizeCatalogMirrorBaseUrl(baseUrl);
    if (normalizedBase.isEmpty())
    {
        return {};
    }

    static const QString kFullCatalogManifestId = QStringLiteral("spcc-gaia-dr3-xp-sampled-full-2026b");
    static const QString kFullCatalogManifestPath =
        QStringLiteral("spcc_gaia_dr3_xp_sampled/local-full/%1/catalog_meta.json").arg(kFullCatalogManifestId);

    return {
        CatalogPackDescriptor{
            .id = kFullCatalogManifestId,
            .displayName = QStringLiteral("SPCC Gaia DR3 XP Sampled Local Full"),
            .description =
                QStringLiteral("AfterNight SPCC local-full catalog pack with 29,648,017 sampled-XP records."),
            .manifestUrl = QUrl(normalizedBase + QLatin1Char('/') + kFullCatalogManifestPath),
            .catalogMetaSha256 = QStringLiteral("1fb5af737d0673c0cb34b4ae7d070094c1c66760d7b9c0739bf2785bbfafc364"),
            .distributionType = QStringLiteral("chunks"),
            .catalogFamilyId = QString::fromLatin1(SPCC_REGIONAL_CACHE_CATALOG_FAMILY_ID),
            .accessProfile = QString::fromLatin1(SPCC_LOCAL_FULL_ACCESS_PROFILE),
            .rightsReviewStatus = QStringLiteral("approved-for-managed-download"),
            .releaseStatus = QStringLiteral("current"),
            .recommended = true,
            .downloadSizeBytes = 45539563436LL,
            .installedSizeBytes = 47690985471LL,
        },
    };
}

[[nodiscard]] inline QVector<CatalogPackDescriptor> availableSpccCatalogPacks()
{
    const std::optional<QJsonObject> config = loadSpccCatalogMirrorConfigObject();
    if (config.has_value())
    {
        QVector<CatalogPackDescriptor> configuredPacks = availableSpccCatalogPacksForMirrorConfig(*config);
        const bool hasLocalFullPack =
            std::any_of(configuredPacks.begin(), configuredPacks.end(), [](const CatalogPackDescriptor& pack) {
                return isDownloadableManagedSpccLocalFullCatalogDescriptor(pack);
            });
        if (!hasLocalFullPack)
        {
            QString fallbackBaseUrl = spccCatalogMirrorBaseUrlFromConfig(*config);
            if (fallbackBaseUrl.isEmpty())
            {
                fallbackBaseUrl = defaultSpccCatalogMirrorBaseUrl();
            }
            const QVector<CatalogPackDescriptor> fallbackPacks = availableSpccCatalogPacksForBaseUrl(fallbackBaseUrl);
            for (const CatalogPackDescriptor& fallbackPack : fallbackPacks)
            {
                const auto duplicate = std::find_if(
                    configuredPacks.begin(), configuredPacks.end(), [&fallbackPack](const CatalogPackDescriptor& pack) {
                        return pack.id.compare(fallbackPack.id, Qt::CaseInsensitive) == 0;
                    });
                if (duplicate == configuredPacks.end())
                {
                    configuredPacks.push_back(fallbackPack);
                }
            }
        }
        if (!configuredPacks.isEmpty())
        {
            return configuredPacks;
        }
    }

    return availableSpccCatalogPacksForBaseUrl(loadSpccCatalogMirrorBaseUrl());
}

[[nodiscard]] inline std::optional<CatalogPackDescriptor>
downloadableManagedSpccCatalogDescriptorForManifest(const QString& manifestId)
{
    const QString normalizedManifestId = manifestId.trimmed();
    if (normalizedManifestId.isEmpty())
    {
        return std::nullopt;
    }

    const QVector<CatalogPackDescriptor> packs = availableSpccCatalogPacks();
    const auto it =
        std::find_if(packs.begin(), packs.end(), [&normalizedManifestId](const CatalogPackDescriptor& pack) {
            return pack.id.compare(normalizedManifestId, Qt::CaseInsensitive) == 0 &&
                   isDownloadableManagedSpccCatalogDescriptor(pack);
        });
    return it == packs.end() ? std::nullopt : std::optional<CatalogPackDescriptor>(*it);
}

[[nodiscard]] inline std::optional<CatalogPackDescriptor> preferredDownloadableManagedSpccLocalFullCatalogDescriptor()
{
    const QVector<CatalogPackDescriptor> packs = availableSpccCatalogPacks();
    auto it = std::find_if(packs.begin(), packs.end(), [](const CatalogPackDescriptor& pack) {
        return pack.recommended && isDownloadableManagedSpccLocalFullCatalogDescriptor(pack);
    });
    if (it == packs.end())
    {
        it = std::find_if(packs.begin(), packs.end(), [](const CatalogPackDescriptor& pack) {
            return isDownloadableManagedSpccLocalFullCatalogDescriptor(pack);
        });
    }
    return it == packs.end() ? std::nullopt : std::optional<CatalogPackDescriptor>(*it);
}

[[nodiscard]] inline QString defaultSpccCatalogPack()
{
    return QString(SettingsDefaults::CATALOGS_SPCC_CATALOG_PACK);
}

[[nodiscard]] inline QString defaultSpccCatalogDirectory()
{
    return CatalogDownloadService::defaultCatalogRootDirectory();
}

[[nodiscard]] inline SpccCatalogDirectoryInspection
inspectSpccCatalogDirectoryDetailed(const QString& directoryPath, const bool verifyChecksums = true)
{
    static const QString kCatalogMetaFileName = QStringLiteral("catalog_meta.json");
    static const QString kInstallCompleteMarker = QStringLiteral(".install_complete");

    SpccCatalogDirectoryInspection inspection;
    inspection.directoryPath = directoryPath.trimmed();

    if (inspection.directoryPath.isEmpty())
    {
        return inspection;
    }

    const QFileInfo directoryInfo(inspection.directoryPath);
    if (!directoryInfo.exists())
    {
        return inspection;
    }
    if (!directoryInfo.isDir())
    {
        inspection.status = SpccCatalogDirectoryStatus::Invalid;
        return inspection;
    }

    const QDir rootDir(inspection.directoryPath);
    inspection.hasInstallMarker = QFileInfo::exists(rootDir.filePath(kInstallCompleteMarker));

    QFile metaFile(rootDir.filePath(kCatalogMetaFileName));
    if (!metaFile.open(QIODevice::ReadOnly))
    {
        const QFileInfoList entries = rootDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
        inspection.status =
            entries.isEmpty() ? SpccCatalogDirectoryStatus::Missing : SpccCatalogDirectoryStatus::Invalid;
        return inspection;
    }

    const QJsonDocument metaDoc = QJsonDocument::fromJson(metaFile.readAll());
    metaFile.close();
    if (!metaDoc.isObject())
    {
        inspection.status = SpccCatalogDirectoryStatus::Invalid;
        return inspection;
    }

    const QJsonObject metaObject = metaDoc.object();
    if (metaObject.value(QStringLiteral("schema_version")).toString() != QStringLiteral("spcc-catalog-meta-v1"))
    {
        inspection.status = SpccCatalogDirectoryStatus::Invalid;
        return inspection;
    }

    inspection.catalogFamilyId = metaObject.value(QStringLiteral("catalog_family_id")).toString().trimmed();
    inspection.manifestId = metaObject.value(QStringLiteral("manifest_id")).toString().trimmed();
    inspection.accessProfile = metaObject.value(QStringLiteral("access_profile")).toString().trimmed();
    inspection.rightsReviewStatus = metaObject.value(QStringLiteral("rights"))
                                        .toObject()
                                        .value(QStringLiteral("review_status"))
                                        .toString()
                                        .trimmed();
    if (inspection.catalogFamilyId.isEmpty() || inspection.manifestId.isEmpty() || inspection.accessProfile.isEmpty())
    {
        inspection.status = SpccCatalogDirectoryStatus::Invalid;
        return inspection;
    }

    const QJsonObject filesObject = metaObject.value(QStringLiteral("files")).toObject();
    struct RequiredSidecar
    {
        QString path;
        QString checksumKey;
    };
    const QVector<RequiredSidecar> sidecars{
        {filesObject.value(QStringLiteral("chunk_manifest")).toString().trimmed(),
         QStringLiteral("chunk_manifest_sha256")},
        {filesObject.value(QStringLiteral("coverage_summary")).toString().trimmed(),
         QStringLiteral("coverage_summary_sha256")},
        {filesObject.value(QStringLiteral("source_id_index")).toString().trimmed(),
         QStringLiteral("source_id_index_sha256")},
        {filesObject.value(QStringLiteral("source_count_transcript")).toString().trimmed(),
         QStringLiteral("source_count_transcript_sha256")},
    };
    for (const RequiredSidecar& sidecar : sidecars)
    {
        if (sidecar.path.isEmpty())
        {
            inspection.status = SpccCatalogDirectoryStatus::Invalid;
            return inspection;
        }
        if (!spcc_catalog_detail::isSafeRelativePath(sidecar.path))
        {
            inspection.status = SpccCatalogDirectoryStatus::Invalid;
            inspection.diagnostic = QObject::tr("SPCC catalog sidecar path is unsafe: %1").arg(sidecar.path);
            return inspection;
        }
        const QFileInfo sidecarInfo(rootDir.filePath(sidecar.path));
        if (!sidecarInfo.exists() || !sidecarInfo.isFile())
        {
            inspection.status = SpccCatalogDirectoryStatus::IncompleteInstall;
            return inspection;
        }
        const QString expectedSidecarSha = metaObject.value(sidecar.checksumKey).toString().trimmed();
        if (verifyChecksums && !expectedSidecarSha.isEmpty() &&
            spcc_catalog_detail::sha256File(sidecarInfo.absoluteFilePath())
                    .compare(expectedSidecarSha, Qt::CaseInsensitive) != 0)
        {
            inspection.status = SpccCatalogDirectoryStatus::IncompleteInstall;
            inspection.checksumsValid = false;
            inspection.diagnostic = QObject::tr("SPCC catalog sidecar checksum mismatch: %1").arg(sidecar.path);
            return inspection;
        }
        ++inspection.sidecarFilesPresent;
    }

    const QJsonArray chunksArray = metaObject.value(QStringLiteral("chunks")).toArray();
    inspection.chunkCount = chunksArray.size();
    if (inspection.chunkCount <= 0)
    {
        inspection.status = SpccCatalogDirectoryStatus::Invalid;
        return inspection;
    }

    const bool localFullInstall =
        inspection.accessProfile.compare(QString::fromLatin1(SPCC_LOCAL_FULL_ACCESS_PROFILE), Qt::CaseInsensitive) == 0;
    const QJsonObject formatObject = metaObject.value(QStringLiteral("format")).toObject();
    inspection.healpixOrder = formatObject.value(QStringLiteral("healpix_order")).toInt(an::kXpCatalogHealpixOrder);
    inspection.chunkParentHealpixOrder =
        formatObject.value(QStringLiteral("chunk_parent_healpix_order")).toInt(an::kXpChunkParentHealpixOrder);
    if (localFullInstall)
    {
        inspection.expectedTileCount = spccCatalogHealpixPixelCountForOrder(inspection.healpixOrder);
        inspection.expectedParentChunkCount = spccCatalogHealpixPixelCountForOrder(inspection.chunkParentHealpixOrder);
        if (inspection.expectedTileCount <= 0 || inspection.expectedParentChunkCount <= 0)
        {
            inspection.status = SpccCatalogDirectoryStatus::Invalid;
            inspection.diagnostic =
                QObject::tr("The local-full SPCC catalog manifest does not declare a valid HEALPix access grid.");
            return inspection;
        }
    }

    for (const QJsonValue& chunkValue : chunksArray)
    {
        if (!chunkValue.isObject())
        {
            inspection.status = SpccCatalogDirectoryStatus::Invalid;
            return inspection;
        }

        const QJsonObject chunkObject = chunkValue.toObject();
        const QString chunkFile = chunkObject.value(QStringLiteral("file")).toString().trimmed();
        if (chunkFile.isEmpty())
        {
            inspection.status = SpccCatalogDirectoryStatus::Invalid;
            return inspection;
        }
        if (!spcc_catalog_detail::isSafeRelativePath(chunkFile))
        {
            inspection.status = SpccCatalogDirectoryStatus::Invalid;
            inspection.diagnostic = QObject::tr("SPCC catalog chunk path is unsafe: %1").arg(chunkFile);
            return inspection;
        }
        const qint64 expectedSize = chunkObject.value(QStringLiteral("size_bytes")).toVariant().toLongLong();
        inspection.chunkFiles.append(chunkFile);
        inspection.chunkSizeBytes.append(expectedSize);

        if (localFullInstall)
        {
            const int firstTile = chunkObject.value(QStringLiteral("first_tile")).toInt(-1);
            const int tileCount = chunkObject.value(QStringLiteral("tile_count")).toInt(-1);
            if (firstTile < 0 || tileCount <= 0 || firstTile + tileCount > inspection.expectedTileCount)
            {
                inspection.status = SpccCatalogDirectoryStatus::Invalid;
                inspection.diagnostic =
                    QObject::tr("The local-full SPCC catalog manifest has an invalid chunk tile range.");
                return inspection;
            }
            inspection.declaredTileCount += tileCount;
            inspection.chunkTileRanges.append(qMakePair(firstTile, tileCount));
        }
        else if (chunkObject.contains(QStringLiteral("first_tile")) &&
                 chunkObject.contains(QStringLiteral("tile_count")))
        {
            const int firstTile = chunkObject.value(QStringLiteral("first_tile")).toInt(-1);
            const int tileCount = chunkObject.value(QStringLiteral("tile_count")).toInt(-1);
            if (firstTile >= 0 && tileCount > 0)
            {
                inspection.declaredTileCount += tileCount;
                inspection.chunkTileRanges.append(qMakePair(firstTile, tileCount));
            }
        }

        const QFileInfo chunkInfo(rootDir.filePath(chunkFile));
        bool chunkPresent =
            chunkInfo.exists() && chunkInfo.isFile() && (expectedSize <= 0 || chunkInfo.size() == expectedSize);
        const QString expectedChunkSha = chunkObject.value(QStringLiteral("sha256")).toString().trimmed();
        if (verifyChecksums && chunkPresent && !expectedChunkSha.isEmpty() &&
            spcc_catalog_detail::sha256File(chunkInfo.absoluteFilePath())
                    .compare(expectedChunkSha, Qt::CaseInsensitive) != 0)
        {
            inspection.checksumsValid = false;
            chunkPresent = false;
        }
        inspection.chunkFilePresent.append(chunkPresent);
        inspection.chunkBytesTotal += (std::max)(qint64{0}, expectedSize);
        if (!chunkPresent)
        {
            ++inspection.chunkFilesMissing;
            inspection.chunkBytesMissing += (std::max)(qint64{0}, expectedSize);
            continue;
        }
        ++inspection.chunkFilesPresent;
        inspection.chunkBytesPresent += (std::max)(qint64{0}, expectedSize);
    }

    if (localFullInstall && inspection.declaredTileCount < inspection.expectedTileCount)
    {
        inspection.status = SpccCatalogDirectoryStatus::IncompleteInstall;
        inspection.diagnostic =
            QObject::tr("Declared chunks %1/%2 and tiles %3/%4; this local-full install does not cover the full sky.")
                .arg(inspection.chunkCount)
                .arg(inspection.expectedParentChunkCount)
                .arg(inspection.declaredTileCount)
                .arg(inspection.expectedTileCount);
        return inspection;
    }

    if (inspection.chunkFilesMissing > 0)
    {
        inspection.status = SpccCatalogDirectoryStatus::PartialCache;
        inspection.diagnostic =
            (inspection.checksumsValid ? QObject::tr("Cached chunks %1/%2; missing %3 catalog chunks.")
                                       : QObject::tr("Cached chunks %1/%2; missing or invalid %3 catalog chunks."))
                .arg(inspection.chunkFilesPresent)
                .arg(inspection.chunkCount)
                .arg(inspection.chunkFilesMissing);
        return inspection;
    }

    inspection.status = SpccCatalogDirectoryStatus::Ready;
    return inspection;
}

[[nodiscard]] inline SpccCatalogDirectoryStatus inspectSpccCatalogDirectory(const QString& directoryPath)
{
    return inspectSpccCatalogDirectoryDetailed(directoryPath).status;
}

[[nodiscard]] inline bool isSpccCatalogReady(const QString& directoryPath)
{
    return inspectSpccCatalogDirectory(directoryPath) == SpccCatalogDirectoryStatus::Ready;
}

/// The access profile a manifest names, or nothing for one this build does not read.
[[nodiscard]] inline std::optional<an::SpectrumCatalogAccessProfile>
spccCatalogAccessProfileFromString(const QString& accessProfile)
{
    if (accessProfile.compare(QString::fromLatin1(SPCC_LOCAL_FULL_ACCESS_PROFILE), Qt::CaseInsensitive) == 0)
    {
        return an::SpectrumCatalogAccessProfile::LocalFullInstall;
    }
    if (accessProfile.compare(QString::fromLatin1(SPCC_REGIONAL_CACHE_ACCESS_PROFILE), Qt::CaseInsensitive) == 0)
    {
        return an::SpectrumCatalogAccessProfile::InstalledRegionalCache;
    }
    return std::nullopt;
}

/// The engine's manifest of an inspected catalog: its identity and the tile range each chunk declares, which is what
/// the chunk selection reads.
[[nodiscard]] inline an::SpectrumCatalogManifest
spccCatalogManifestFromInspection(const SpccCatalogDirectoryInspection& inspection)
{
    an::SpectrumCatalogManifest manifest;
    manifest.catalogFamilyId = inspection.catalogFamilyId.toStdString();
    manifest.manifestId = inspection.manifestId.toStdString();
    manifest.accessProfile = spccCatalogAccessProfileFromString(inspection.accessProfile)
                                 .value_or(an::SpectrumCatalogAccessProfile::InstalledRegionalCache);
    manifest.rightsReviewStatus = inspection.rightsReviewStatus.toStdString();
    manifest.healpixOrder = static_cast<std::uint8_t>(inspection.healpixOrder);
    manifest.chunkParentHealpixOrder = static_cast<std::uint8_t>(inspection.chunkParentHealpixOrder);
    manifest.chunks.reserve(static_cast<std::size_t>(inspection.chunkTileRanges.size()));
    for (int index = 0; index < inspection.chunkTileRanges.size(); ++index)
    {
        const auto& range = inspection.chunkTileRanges[index];
        an::SpectrumChunkDescriptor chunk;
        chunk.chunkIndex = index;
        chunk.firstTile = range.first;
        chunk.tileCount = range.second;
        if (index < inspection.chunkFiles.size())
        {
            chunk.file = inspection.chunkFiles[index].toStdString();
        }
        if (index < inspection.chunkSizeBytes.size() && inspection.chunkSizeBytes[index] > 0)
        {
            chunk.sizeBytes = static_cast<std::uint64_t>(inspection.chunkSizeBytes[index]);
        }
        manifest.chunks.push_back(std::move(chunk));
    }
    return manifest;
}

/// Which chunks a target region and a set of Gaia sources need, and which of them the installed catalog holds.
///
/// @param execution the operation the engine's chunk selection runs in.
/// @param inspection the installed catalog.
/// @param centerRaDeg the region's centre.
/// @param centerDecDeg the region's centre.
/// @param radiusDeg the region's radius.
/// @param sourceIds Gaia sources whose chunks are needed too.
/// @return the availability; not valid when the catalog cannot be planned against.
[[nodiscard]] inline SpccCatalogRequiredChunkAvailability spccCatalogRequiredChunkAvailabilityForRegion(
    const an::ExecutionContext& execution, const SpccCatalogDirectoryInspection& inspection, const double centerRaDeg,
    const double centerDecDeg, const double radiusDeg, const std::vector<std::uint64_t>& sourceIds = {})
{
    SpccCatalogRequiredChunkAvailability availability;
    if (inspection.status == SpccCatalogDirectoryStatus::Missing ||
        inspection.status == SpccCatalogDirectoryStatus::Invalid ||
        inspection.status == SpccCatalogDirectoryStatus::IncompleteInstall)
    {
        availability.diagnostic = QObject::tr("SPCC catalog metadata is not valid enough to plan required chunks.");
        return availability;
    }
    if (inspection.chunkTileRanges.size() != inspection.chunkCount)
    {
        availability.diagnostic = QObject::tr("SPCC catalog metadata does not include complete chunk tile coverage.");
        return availability;
    }

    const an::SpectrumCatalogManifest manifest = spccCatalogManifestFromInspection(inspection);
    const an::Result<an::SpectrumChunkSelection> region = an::selectSpectrumChunksForRegion(
        execution, manifest,
        an::SkyCoordinate{.rightAscensionDegrees = centerRaDeg, .declinationDegrees = centerDecDeg}, radiusDeg);
    const an::Result<std::vector<std::uint32_t>> sources =
        an::selectSpectrumChunksForSources(execution, manifest, sourceIds);
    if (!region.hasValue() || !sources.hasValue())
    {
        availability.diagnostic =
            QObject::tr("SPCC catalog chunks could not be planned for this target region: %1")
                .arg(QString::fromUtf8(region.hasValue() ? sources.status().message() : region.status().message()));
        return availability;
    }
    std::vector<std::uint32_t> required = region->chunkIndices;
    for (const std::uint32_t sourceChunk : *sources)
    {
        if (std::find(required.begin(), required.end(), sourceChunk) == required.end())
        {
            required.push_back(sourceChunk);
        }
    }

    availability.valid = true;
    for (const std::uint32_t chunkIndex : required)
    {
        if (chunkIndex >= static_cast<std::uint32_t>(inspection.chunkCount))
        {
            continue;
        }
        const int chunk = static_cast<int>(chunkIndex);
        availability.requiredChunkIndices.append(chunk);
        if (chunk < inspection.chunkFilePresent.size() && inspection.chunkFilePresent[chunk])
        {
            availability.cachedChunkIndices.append(chunk);
            continue;
        }

        availability.missingChunkIndices.append(chunk);
        if (chunk < inspection.chunkSizeBytes.size())
        {
            availability.missingBytes += (std::max)(qint64{0}, inspection.chunkSizeBytes[chunk]);
        }
    }
    if (availability.requiredChunkIndices.isEmpty())
    {
        availability.valid = false;
        availability.diagnostic = QObject::tr("SPCC catalog metadata does not declare chunks for this target region.");
    }
    return availability;
}

[[nodiscard]] inline SpccCatalogDirectoryInspection inspectSpccCatalogPackInstall(const QString& catalogRootDirectory,
                                                                                  const QString& manifestId,
                                                                                  const bool verifyChecksums = true)
{
    const QString normalizedRoot = catalogRootDirectory.trimmed();
    const QString normalizedManifest = manifestId.trimmed();
    if (normalizedRoot.isEmpty() || normalizedManifest.isEmpty())
    {
        return {};
    }

    return inspectSpccCatalogDirectoryDetailed(QDir(normalizedRoot).filePath(normalizedManifest), verifyChecksums);
}

[[nodiscard]] inline bool spccCatalogPackInstallSatisfiesRegionalRequest(const CatalogPackDescriptor& descriptor,
                                                                         const QString& catalogRootDirectory,
                                                                         const SpccRegionalCatalogRequest& request,
                                                                         QString* rejectionReason = nullptr)
{
    auto reject = [rejectionReason](const QString& reason) {
        if (rejectionReason)
        {
            *rejectionReason = reason;
        }
        return false;
    };

    if (rejectionReason)
    {
        rejectionReason->clear();
    }

    if (!spccCatalogPackDescriptorCoversRegionalRequest(descriptor, request, rejectionReason))
    {
        return false;
    }

    const SpccCatalogDirectoryInspection inspection =
        inspectSpccCatalogPackInstall(catalogRootDirectory, descriptor.id);
    if (inspection.status != SpccCatalogDirectoryStatus::Ready)
    {
        return reject(QObject::tr("The selected SPCC regional catalog pack is not installed and ready."));
    }
    if (inspection.manifestId.compare(descriptor.id, Qt::CaseInsensitive) != 0)
    {
        return reject(QObject::tr("The installed SPCC regional catalog manifest does not match the descriptor."));
    }
    if (inspection.catalogFamilyId.compare(request.catalogFamilyId, Qt::CaseInsensitive) != 0)
    {
        return reject(QObject::tr("The installed SPCC regional catalog family does not match the request."));
    }
    if (inspection.accessProfile.compare(request.accessProfile, Qt::CaseInsensitive) != 0)
    {
        return reject(QObject::tr("The installed SPCC regional catalog access profile does not match the request."));
    }
    if (!isApprovedSpccCatalogDescriptorRightsStatus(inspection.rightsReviewStatus))
    {
        return reject(QObject::tr("The installed SPCC regional catalog is not rights-approved for managed use."));
    }

    return true;
}

[[nodiscard]] inline QVector<SpccCatalogPackLocation> scanSpccCatalogPacks(const QString& directoryPath,
                                                                           const bool verifyChecksums = true)
{
    QVector<SpccCatalogPackLocation> packs;
    const QString normalizedPath = directoryPath.trimmed();
    if (normalizedPath.isEmpty())
    {
        return packs;
    }

    const QFileInfo rootInfo(normalizedPath);
    if (!rootInfo.exists() || !rootInfo.isDir())
    {
        return packs;
    }

    auto appendPack = [&packs](const QString& path, const QString& fallbackId,
                               const SpccCatalogDirectoryInspection& inspection) {
        const QString manifestId = !inspection.manifestId.isEmpty() ? inspection.manifestId : fallbackId.trimmed();
        if (manifestId.isEmpty())
        {
            return;
        }

        packs.push_back(SpccCatalogPackLocation{
            .manifestId = manifestId,
            .directoryPath = path,
            .displayName = manifestId,
            .inspection = inspection,
        });
    };

    const SpccCatalogDirectoryInspection rootInspection =
        inspectSpccCatalogDirectoryDetailed(normalizedPath, verifyChecksums);
    if (!rootInspection.manifestId.isEmpty() || rootInspection.status == SpccCatalogDirectoryStatus::Ready ||
        rootInspection.status == SpccCatalogDirectoryStatus::PartialCache ||
        rootInspection.status == SpccCatalogDirectoryStatus::IncompleteInstall)
    {
        appendPack(normalizedPath, rootInfo.fileName(), rootInspection);
        return packs;
    }

    struct PendingDirectory
    {
        QString path;
        int depth = 0;
    };

    QVector<PendingDirectory> pending;
    const QDir rootDir(normalizedPath);
    for (const QFileInfo& childDir : rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
        pending.push_back(PendingDirectory{.path = childDir.absoluteFilePath(), .depth = 1});
    }

    while (!pending.isEmpty())
    {
        const PendingDirectory current = pending.takeFirst();
        const SpccCatalogDirectoryInspection inspection =
            inspectSpccCatalogDirectoryDetailed(current.path, verifyChecksums);
        if (inspection.manifestId.isEmpty() && inspection.status != SpccCatalogDirectoryStatus::Ready &&
            inspection.status != SpccCatalogDirectoryStatus::PartialCache &&
            inspection.status != SpccCatalogDirectoryStatus::IncompleteInstall)
        {
            if (current.depth < 3)
            {
                const QDir currentDir(current.path);
                for (const QFileInfo& childDir :
                     currentDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
                {
                    pending.push_back(
                        PendingDirectory{.path = childDir.absoluteFilePath(), .depth = current.depth + 1});
                }
            }
            continue;
        }

        appendPack(current.path, QFileInfo(current.path).fileName(), inspection);
    }

    return packs;
}

[[nodiscard]] inline QString resolveSpccCatalogDirectory(const QString& catalogRootDirectory, const QString& manifestId,
                                                         const bool verifyChecksums = true)
{
    const QString configuredDirectory = catalogRootDirectory.trimmed();
    const QString pack = manifestId.trimmed().isEmpty() ? defaultSpccCatalogPack() : manifestId.trimmed();
    if (!configuredDirectory.isEmpty())
    {
        const SpccCatalogDirectoryInspection inspection =
            inspectSpccCatalogDirectoryDetailed(configuredDirectory, verifyChecksums);
        if (inspection.status != SpccCatalogDirectoryStatus::Missing &&
            inspection.status != SpccCatalogDirectoryStatus::Invalid)
        {
            return configuredDirectory;
        }

        const QVector<SpccCatalogPackLocation> packs = scanSpccCatalogPacks(configuredDirectory, verifyChecksums);
        for (const SpccCatalogPackLocation& discoveredPack : packs)
        {
            if (discoveredPack.manifestId == pack)
            {
                return discoveredPack.directoryPath;
            }
        }
        if (packs.size() == 1)
        {
            return packs.front().directoryPath;
        }

        if (pack.isEmpty())
        {
            return {};
        }

        return QDir(configuredDirectory).filePath(pack);
    }

    const QString defaultDirectory = defaultSpccCatalogDirectory();
    return pack.isEmpty() ? QString() : QDir(defaultDirectory).filePath(pack);
}

[[nodiscard]] inline QString resolveSpccCatalogPack(GlobalSettingsPersistence* settings)
{
    if (!settings)
    {
        return defaultSpccCatalogPack();
    }

    const QString configuredPack =
        settings
            ->select(SettingsKeys::CATALOGS_SPCC_CATALOG_PACK, QString(SettingsDefaults::CATALOGS_SPCC_CATALOG_PACK))
            .trimmed();
    return configuredPack.isEmpty() ? defaultSpccCatalogPack() : configuredPack;
}

[[nodiscard]] inline QString resolveSpccCatalogPack(IGlobalSettingsProvider* settings)
{
    if (!settings)
    {
        return defaultSpccCatalogPack();
    }

    const QString configuredPack =
        settings
            ->select(SettingsKeys::CATALOGS_SPCC_CATALOG_PACK, QString(SettingsDefaults::CATALOGS_SPCC_CATALOG_PACK))
            .trimmed();
    return configuredPack.isEmpty() ? defaultSpccCatalogPack() : configuredPack;
}

[[nodiscard]] inline QString resolveSpccCatalogDirectory(GlobalSettingsPersistence* settings,
                                                         const bool verifyChecksums = true)
{
    if (!settings)
    {
        return resolveSpccCatalogDirectory(QString(), defaultSpccCatalogPack(), verifyChecksums);
    }

    const QString configuredDirectory = settings
                                            ->select(SettingsKeys::CATALOGS_SPCC_CATALOG_DIRECTORY,
                                                     QString(SettingsDefaults::CATALOGS_SPCC_CATALOG_DIRECTORY))
                                            .trimmed();
    return resolveSpccCatalogDirectory(configuredDirectory, resolveSpccCatalogPack(settings), verifyChecksums);
}

[[nodiscard]] inline QString resolveSpccCatalogDirectory(IGlobalSettingsProvider* settings,
                                                         const bool verifyChecksums = true)
{
    if (!settings)
    {
        return resolveSpccCatalogDirectory(QString(), defaultSpccCatalogPack(), verifyChecksums);
    }

    const QString configuredDirectory = settings
                                            ->select(SettingsKeys::CATALOGS_SPCC_CATALOG_DIRECTORY,
                                                     QString(SettingsDefaults::CATALOGS_SPCC_CATALOG_DIRECTORY))
                                            .trimmed();
    return resolveSpccCatalogDirectory(configuredDirectory, resolveSpccCatalogPack(settings), verifyChecksums);
}

[[nodiscard]] inline SpccCatalogDirectoryStatus inspectSpccCatalogDirectory(GlobalSettingsPersistence* settings)
{
    return inspectSpccCatalogDirectory(resolveSpccCatalogDirectory(settings));
}

[[nodiscard]] inline SpccCatalogDirectoryStatus inspectSpccCatalogDirectory(IGlobalSettingsProvider* settings)
{
    return inspectSpccCatalogDirectory(resolveSpccCatalogDirectory(settings));
}

} // namespace an::ui
