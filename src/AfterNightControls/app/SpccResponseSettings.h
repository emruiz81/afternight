// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "controls/GlobalSettingsPersistence.h"
#include "controls/IAppContext.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightPlatform/Sha256.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVector>

#include "app/ApplicationStorage.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>

namespace an::ui
{

enum class SpccResponseRepositoryStatus
{
    Missing,
    Ready,
    IncompleteInstall,
    Invalid,
};

struct SpccResponseRepositoryInspection
{
    SpccResponseRepositoryStatus status = SpccResponseRepositoryStatus::Missing;
    QString directoryPath;
    QString repositoryId;
    QString version;
    QString rightsReviewStatus;
    QString diagnostic;
    int componentCount = 0;
    int whiteReferenceCount = 0;
    int throughputProfileCount = 0;
    bool hasRuntimeDeploymentScope = false;
};

struct SpccResponseProfileMapping
{
    QString cameraModel;
    QString filterName;
    QString repositoryId;
    QString aliasRegistryVersion;
    QString profileId;
    QString whiteReferenceId;
};

struct SpccResponseRepositoryDescriptor
{
    QString id;
    QString repositoryId;
    QString version;
    QString displayName;
    QString description;
    QUrl manifestUrl;
    QString rightsReviewStatus;
    QString deploymentScope;
    QString responseMetaSha256;
    bool recommended = false;
    qint64 downloadSizeBytes = 0;
    qint64 installedSizeBytes = 0;
    QString releaseStatus;
};

[[nodiscard]] inline QString defaultSpccResponseRepositoryDirectory()
{
    const QString base = applicationLocalDataPath();
    return QDir(base).filePath(QStringLiteral("spcc_responses"));
}

namespace spcc_response_detail
{

[[nodiscard]] inline QString normalizedMappingToken(const QString& value)
{
    return value.trimmed().toCaseFolded();
}

[[nodiscard]] inline bool isSha256Hex(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.size() != 64)
    {
        return false;
    }
    for (const QChar ch : trimmed)
    {
        if (!ch.isDigit() && (ch.toLower() < QLatin1Char('a') || ch.toLower() > QLatin1Char('f')))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline QString normalizeMirrorBaseUrl(const QString& baseUrl)
{
    const QString trimmed = baseUrl.trimmed();
    return trimmed.endsWith(QLatin1Char('/')) ? trimmed.chopped(1) : trimmed;
}

[[nodiscard]] inline bool mappingMatches(const SpccResponseProfileMapping& mapping, const QString& cameraModel,
                                         const QString& filterName, const QString& repositoryId,
                                         const QString& aliasRegistryVersion)
{
    return normalizedMappingToken(mapping.cameraModel) == normalizedMappingToken(cameraModel) &&
           normalizedMappingToken(mapping.filterName) == normalizedMappingToken(filterName) &&
           normalizedMappingToken(mapping.repositoryId) == normalizedMappingToken(repositoryId) &&
           normalizedMappingToken(mapping.aliasRegistryVersion) == normalizedMappingToken(aliasRegistryVersion);
}

[[nodiscard]] inline QString sha256File(const QString& path)
{
    const std::optional<std::array<std::uint8_t, 32>> digest =
        an::platform::sha256OfFile(std::filesystem::path(path.toStdWString()));
    return digest.has_value() ? QString::fromStdString(an::platform::sha256Hex(*digest)) : QString();
}

[[nodiscard]] inline QJsonDocument readJsonFile(const QString& path, bool* ok)
{
    if (ok)
    {
        *ok = false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        return {};
    }

    if (ok)
    {
        *ok = true;
    }
    return doc;
}

[[nodiscard]] inline bool hasRuntimeScope(const QJsonArray& scopes)
{
    for (const QJsonValue& scopeValue : scopes)
    {
        const QString scope = scopeValue.toString();
        if (scope == QStringLiteral("deployed-runtime") || scope == QStringLiteral("managed-cache") ||
            scope == QStringLiteral("user-private"))
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline QSet<QString> validateAssets(const QDir& repositoryDir, const QJsonArray& assets,
                                                  const QString& idKey, SpccResponseRepositoryInspection& inspection)
{
    QSet<QString> assetIds;
    for (const QJsonValue& assetValue : assets)
    {
        if (!assetValue.isObject())
        {
            inspection.status = SpccResponseRepositoryStatus::Invalid;
            inspection.diagnostic = QObject::tr("Response repository contains a non-object asset entry.");
            return {};
        }

        const QJsonObject asset = assetValue.toObject();
        const QString assetId = asset.value(idKey).toString().trimmed();
        const QString relativeFile = asset.value(QStringLiteral("file")).toString().trimmed();
        if (assetId.isEmpty() || relativeFile.isEmpty())
        {
            inspection.status = SpccResponseRepositoryStatus::Invalid;
            inspection.diagnostic = QObject::tr("Response repository asset is missing an ID or file path.");
            return {};
        }
        assetIds.insert(assetId);

        const QFileInfo assetInfo(repositoryDir.filePath(relativeFile));
        if (!assetInfo.exists() || !assetInfo.isFile())
        {
            inspection.status = SpccResponseRepositoryStatus::IncompleteInstall;
            inspection.diagnostic = QObject::tr("Response asset file is missing: %1").arg(relativeFile);
            return {};
        }

        const QString expectedFileSha = asset.value(QStringLiteral("file_sha256")).toString().trimmed();
        if (!expectedFileSha.isEmpty() &&
            sha256File(assetInfo.absoluteFilePath()).compare(expectedFileSha, Qt::CaseInsensitive) != 0)
        {
            inspection.status = SpccResponseRepositoryStatus::IncompleteInstall;
            inspection.diagnostic = QObject::tr("Response asset checksum mismatch: %1").arg(relativeFile);
            return {};
        }

        bool payloadOk = false;
        const QJsonDocument payloadDoc = readJsonFile(assetInfo.absoluteFilePath(), &payloadOk);
        if (!payloadOk)
        {
            inspection.status = SpccResponseRepositoryStatus::Invalid;
            inspection.diagnostic = QObject::tr("Response asset is not valid JSON: %1").arg(relativeFile);
            return {};
        }

        const QJsonObject payload = payloadDoc.object();
        if (payload.value(QStringLiteral("schema_version")).toString() != QStringLiteral("spcc-response-asset-v1"))
        {
            inspection.status = SpccResponseRepositoryStatus::Invalid;
            inspection.diagnostic = QObject::tr("Response asset has an unsupported schema: %1").arg(relativeFile);
            return {};
        }
        if (payload.value(idKey).toString().trimmed() != assetId)
        {
            inspection.status = SpccResponseRepositoryStatus::Invalid;
            inspection.diagnostic = QObject::tr("Response asset ID mismatch: %1").arg(relativeFile);
            return {};
        }
        if (payload.value(QStringLiteral("wavelength_unit")).toString() != QStringLiteral("nm") ||
            payload.value(QStringLiteral("value_semantics")).toString().trimmed().isEmpty())
        {
            inspection.status = SpccResponseRepositoryStatus::Invalid;
            inspection.diagnostic =
                QObject::tr("Response asset has invalid units or value semantics: %1").arg(relativeFile);
            return {};
        }

        const QString expectedDataSha = asset.value(QStringLiteral("data_sha256")).toString().trimmed();
        const QString payloadDataSha = payload.value(QStringLiteral("data_sha256")).toString().trimmed();
        if (expectedDataSha.isEmpty() || payloadDataSha.isEmpty() ||
            expectedDataSha.compare(payloadDataSha, Qt::CaseInsensitive) != 0)
        {
            inspection.status = SpccResponseRepositoryStatus::Invalid;
            inspection.diagnostic = QObject::tr("Response asset data checksum metadata mismatch: %1").arg(relativeFile);
            return {};
        }
    }

    return assetIds;
}

} // namespace spcc_response_detail

[[nodiscard]] inline bool isApprovedSpccResponseDescriptorRightsStatus(const QString& rightsReviewStatus)
{
    const QString normalized = rightsReviewStatus.trimmed();
    return normalized.compare(QStringLiteral("approved-for-managed-download"), Qt::CaseInsensitive) == 0 ||
           normalized.compare(QStringLiteral("approved-for-bundling"), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] inline bool isSupportedSpccResponseDeploymentScope(const QString& deploymentScope)
{
    const QString normalized = deploymentScope.trimmed();
    return normalized.compare(QStringLiteral("managed-cache"), Qt::CaseInsensitive) == 0 ||
           normalized.compare(QStringLiteral("deployed-runtime"), Qt::CaseInsensitive) == 0 ||
           normalized.compare(QStringLiteral("installer"), Qt::CaseInsensitive) == 0 ||
           normalized.compare(QStringLiteral("appimage"), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] inline QString defaultSpccResponseRepositoryConfigBaseUrl()
{
    return QStringLiteral("https://huggingface.co/datasets/emruiz81/afternight-spcc-responses/resolve/main");
}

[[nodiscard]] inline QString spccResponseRepositoryConfigFilePath()
{
    static const QString kFileName = QStringLiteral("spcc_response_repositories.json");

    const QString dataDir = applicationLocalDataPath();
    return dataDir.isEmpty() ? QString() : QDir(dataDir).filePath(kFileName);
}

[[nodiscard]] inline QString spccResponseRepositoryConfigBaseUrlFromConfig(const QJsonObject& config)
{
    const QJsonArray mirrors = config.value(QStringLiteral("mirrors")).toArray();
    for (const QJsonValue& mirrorValue : mirrors)
    {
        const QString url = spcc_response_detail::normalizeMirrorBaseUrl(mirrorValue.toString());
        if (!url.isEmpty())
        {
            return url;
        }
    }

    return {};
}

[[nodiscard]] inline std::optional<QJsonObject> loadSpccResponseRepositoryConfigObject()
{
    const QString filePath = spccResponseRepositoryConfigFilePath();
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

[[nodiscard]] inline QString normalizeRelativeSpccResponseRepositoryManifestPath(const QString& manifestPath)
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
        !cleaned.endsWith(QStringLiteral("response_meta.json")))
    {
        return {};
    }

    return cleaned;
}

[[nodiscard]] inline QUrl spccResponseRepositoryDescriptorManifestUrl(const QJsonObject& descriptorObject,
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

    const QString manifestPath = normalizeRelativeSpccResponseRepositoryManifestPath(
        descriptorObject.value(QStringLiteral("manifest_path")).toString());
    if (manifestPath.isEmpty())
    {
        return {};
    }

    const QString base = spcc_response_detail::normalizeMirrorBaseUrl(mirrorBaseUrl);
    if (base.isEmpty())
    {
        return {};
    }

    const QUrl baseUrl(base + QLatin1Char('/'));
    const QUrl resolved = baseUrl.resolved(QUrl(manifestPath));
    return resolved.isValid() && !resolved.isRelative() ? resolved : QUrl();
}

[[nodiscard]] inline std::optional<SpccResponseRepositoryDescriptor>
spccResponseRepositoryDescriptorFromConfigObject(const QJsonObject& descriptorObject, const QString& mirrorBaseUrl,
                                                 QString* rejectionReason = nullptr)
{
    auto reject = [rejectionReason](const QString& reason) -> std::optional<SpccResponseRepositoryDescriptor> {
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

    SpccResponseRepositoryDescriptor descriptor;
    descriptor.id = descriptorObject.value(QStringLiteral("id")).toString().trimmed();
    descriptor.repositoryId = descriptorObject.value(QStringLiteral("repository_id")).toString().trimmed();
    descriptor.version = descriptorObject.value(QStringLiteral("version")).toString().trimmed();
    descriptor.displayName = descriptorObject.value(QStringLiteral("display_name")).toString().trimmed();
    descriptor.description = descriptorObject.value(QStringLiteral("description")).toString().trimmed();
    descriptor.manifestUrl = spccResponseRepositoryDescriptorManifestUrl(descriptorObject, mirrorBaseUrl);
    descriptor.rightsReviewStatus = descriptorObject.value(QStringLiteral("rights_review_status")).toString().trimmed();
    if (descriptor.rightsReviewStatus.isEmpty())
    {
        descriptor.rightsReviewStatus = descriptorObject.value(QStringLiteral("rights"))
                                            .toObject()
                                            .value(QStringLiteral("review_status"))
                                            .toString()
                                            .trimmed();
    }
    descriptor.deploymentScope =
        descriptorObject.value(QStringLiteral("deployment_scope")).toString(QStringLiteral("managed-cache")).trimmed();
    descriptor.responseMetaSha256 = descriptorObject.value(QStringLiteral("response_meta_sha256")).toString().trimmed();
    descriptor.recommended = descriptorObject.value(QStringLiteral("recommended")).toBool(false);
    descriptor.downloadSizeBytes =
        descriptorObject.value(QStringLiteral("download_size_bytes")).toVariant().toLongLong();
    descriptor.installedSizeBytes =
        descriptorObject.value(QStringLiteral("installed_size_bytes")).toVariant().toLongLong();
    descriptor.releaseStatus =
        descriptorObject.value(QStringLiteral("release_status")).toString(QStringLiteral("current")).trimmed();

    if (descriptor.id.isEmpty())
    {
        return reject(QObject::tr("The SPCC response repository descriptor is missing an ID."));
    }
    if (descriptor.repositoryId.isEmpty() || descriptor.version.isEmpty())
    {
        return reject(QObject::tr("The SPCC response repository descriptor is missing repository identity."));
    }
    if (descriptor.displayName.isEmpty())
    {
        descriptor.displayName = descriptor.id;
    }
    if (!descriptor.manifestUrl.isValid())
    {
        return reject(QObject::tr("The SPCC response repository descriptor has an invalid manifest URL."));
    }
    if (!isApprovedSpccResponseDescriptorRightsStatus(descriptor.rightsReviewStatus))
    {
        return reject(QObject::tr("The SPCC response repository descriptor is not rights-approved."));
    }
    if (!isSupportedSpccResponseDeploymentScope(descriptor.deploymentScope))
    {
        return reject(QObject::tr("The SPCC response repository descriptor has an unsupported deployment scope."));
    }
    if (!descriptor.responseMetaSha256.isEmpty() && !spcc_response_detail::isSha256Hex(descriptor.responseMetaSha256))
    {
        return reject(QObject::tr("The SPCC response repository descriptor has an invalid manifest checksum."));
    }
    if (descriptor.downloadSizeBytes < 0 || descriptor.installedSizeBytes < 0)
    {
        return reject(QObject::tr("The SPCC response repository descriptor has an invalid byte size."));
    }
    if (descriptor.releaseStatus.compare(QStringLiteral("current"), Qt::CaseInsensitive) != 0)
    {
        return reject(QObject::tr("The SPCC response repository descriptor is not current."));
    }

    return descriptor;
}

[[nodiscard]] inline QVector<SpccResponseRepositoryDescriptor>
availableSpccResponseRepositoriesForConfig(const QJsonObject& config, QString* diagnostic = nullptr)
{
    if (diagnostic)
    {
        diagnostic->clear();
    }
    if (config.value(QStringLiteral("schema_version")).toString() !=
        QStringLiteral("spcc-response-repository-config-v1"))
    {
        if (diagnostic)
        {
            *diagnostic = QObject::tr("The SPCC response repository config has an unsupported schema.");
        }
        return {};
    }

    const QString mirrorBaseUrl = spccResponseRepositoryConfigBaseUrlFromConfig(config);
    const QJsonArray descriptorArray = config.value(QStringLiteral("response_repositories")).toArray();
    QVector<SpccResponseRepositoryDescriptor> repositories;
    QSet<QString> seenDescriptorIds;
    QSet<QString> seenRepositoryVersions;
    int rejectedCount = 0;
    QString lastRejectionReason;
    for (const QJsonValue& descriptorValue : descriptorArray)
    {
        if (!descriptorValue.isObject())
        {
            ++rejectedCount;
            lastRejectionReason = QObject::tr("A SPCC response repository descriptor entry is not a JSON object.");
            continue;
        }

        QString rejectionReason;
        const std::optional<SpccResponseRepositoryDescriptor> descriptor =
            spccResponseRepositoryDescriptorFromConfigObject(descriptorValue.toObject(), mirrorBaseUrl,
                                                             &rejectionReason);
        if (!descriptor.has_value())
        {
            ++rejectedCount;
            lastRejectionReason = rejectionReason;
            continue;
        }

        const QString descriptorIdKey = descriptor->id.toCaseFolded();
        const QString repositoryVersionKey =
            QStringLiteral("%1|%2").arg(descriptor->repositoryId.toCaseFolded(), descriptor->version.toCaseFolded());
        if (seenDescriptorIds.contains(descriptorIdKey) || seenRepositoryVersions.contains(repositoryVersionKey))
        {
            ++rejectedCount;
            lastRejectionReason = QObject::tr("A duplicate SPCC response repository descriptor was ignored.");
            continue;
        }

        seenDescriptorIds.insert(descriptorIdKey);
        seenRepositoryVersions.insert(repositoryVersionKey);
        repositories.push_back(*descriptor);
    }

    std::stable_sort(repositories.begin(), repositories.end(),
                     [](const SpccResponseRepositoryDescriptor& lhs, const SpccResponseRepositoryDescriptor& rhs) {
                         if (lhs.recommended != rhs.recommended)
                         {
                             return lhs.recommended;
                         }
                         return lhs.displayName.compare(rhs.displayName, Qt::CaseInsensitive) < 0;
                     });

    if (diagnostic && rejectedCount > 0)
    {
        *diagnostic = QObject::tr("%1 SPCC response repository descriptor(s) were ignored. %2")
                          .arg(rejectedCount)
                          .arg(lastRejectionReason);
    }
    return repositories;
}

[[nodiscard]] inline QVector<SpccResponseRepositoryDescriptor>
availableSpccResponseRepositoriesForConfigJson(const QByteArray& configBytes, QString* diagnostic = nullptr)
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
            *diagnostic = QObject::tr("The SPCC response repository config is not valid JSON.");
        }
        return {};
    }

    return availableSpccResponseRepositoriesForConfig(document.object(), diagnostic);
}

[[nodiscard]] inline QString loadSpccResponseRepositoryConfigBaseUrl()
{
    const std::optional<QJsonObject> config = loadSpccResponseRepositoryConfigObject();
    if (config.has_value())
    {
        const QString baseUrl = spccResponseRepositoryConfigBaseUrlFromConfig(*config);
        if (!baseUrl.isEmpty())
        {
            return baseUrl;
        }
    }

    return defaultSpccResponseRepositoryConfigBaseUrl();
}

[[nodiscard]] inline QVector<SpccResponseRepositoryDescriptor>
availableSpccResponseRepositoriesForBaseUrl(const QString& baseUrl)
{
    const QString normalizedBase = spcc_response_detail::normalizeMirrorBaseUrl(baseUrl);
    if (normalizedBase.isEmpty())
    {
        return {};
    }

    static const QString kRepositoryDescriptorId = QStringLiteral("afternight-spcc-response-baseline-2026a");
    static const QString kRepositoryId = QStringLiteral("afternight-spcc-response-baseline");
    static const QString kRepositoryVersion = QStringLiteral("2026a");
    static const QString kRepositoryManifestPath =
        QStringLiteral("responses/%1/%2/response_meta.json").arg(kRepositoryId, kRepositoryVersion);

    return {
        SpccResponseRepositoryDescriptor{
            .id = kRepositoryDescriptorId,
            .repositoryId = kRepositoryId,
            .version = kRepositoryVersion,
            .displayName = QStringLiteral("AfterNight SPCC Response Baseline 2026a"),
            .description = QStringLiteral("Reviewed AfterNight SPCC response baseline 2026a."),
            .manifestUrl = QUrl(normalizedBase + QLatin1Char('/') + kRepositoryManifestPath),
            .rightsReviewStatus = QStringLiteral("approved-for-managed-download"),
            .deploymentScope = QStringLiteral("managed-cache"),
            .responseMetaSha256 = QStringLiteral("24bfdcb7eefdf6c89ccd568db11ebbfc625e6138fc4b5ebcba7d78558b006bc9"),
            .recommended = true,
            .downloadSizeBytes = 27573635LL,
            .installedSizeBytes = 27573635LL,
            .releaseStatus = QStringLiteral("current"),
        },
    };
}

[[nodiscard]] inline QVector<SpccResponseRepositoryDescriptor> availableSpccResponseRepositories()
{
    const std::optional<QJsonObject> config = loadSpccResponseRepositoryConfigObject();
    if (config.has_value())
    {
        const QVector<SpccResponseRepositoryDescriptor> configuredRepositories =
            availableSpccResponseRepositoriesForConfig(*config);
        if (!configuredRepositories.isEmpty())
        {
            return configuredRepositories;
        }
    }

    return availableSpccResponseRepositoriesForBaseUrl(loadSpccResponseRepositoryConfigBaseUrl());
}

[[nodiscard]] inline QVector<SpccResponseProfileMapping>
loadSpccResponseProfileMappings(IGlobalSettingsProvider* settings)
{
    if (!settings)
    {
        return {};
    }

    const QString rawMappings = settings
                                    ->select(SettingsKeys::CATALOGS_SPCC_RESPONSE_PROFILE_MAPPINGS,
                                             QString(SettingsDefaults::CATALOGS_SPCC_RESPONSE_PROFILE_MAPPINGS))
                                    .trimmed();
    if (rawMappings.isEmpty())
    {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(rawMappings.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray())
    {
        return {};
    }

    QVector<SpccResponseProfileMapping> mappings;
    const QJsonArray array = doc.array();
    mappings.reserve(array.size());
    for (const QJsonValue& value : array)
    {
        if (!value.isObject())
        {
            continue;
        }

        const QJsonObject object = value.toObject();
        SpccResponseProfileMapping mapping{
            .cameraModel = object.value(QStringLiteral("camera_model")).toString().trimmed(),
            .filterName = object.value(QStringLiteral("filter")).toString().trimmed(),
            .repositoryId = object.value(QStringLiteral("repository_id")).toString().trimmed(),
            .aliasRegistryVersion = object.value(QStringLiteral("alias_registry_version")).toString().trimmed(),
            .profileId = object.value(QStringLiteral("profile_id")).toString().trimmed(),
            .whiteReferenceId = object.value(QStringLiteral("white_reference_id")).toString().trimmed(),
        };
        if (!mapping.cameraModel.isEmpty() && !mapping.profileId.isEmpty() && !mapping.repositoryId.isEmpty() &&
            !mapping.aliasRegistryVersion.isEmpty())
        {
            mappings.push_back(std::move(mapping));
        }
    }
    return mappings;
}

[[nodiscard]] inline std::optional<SpccResponseProfileMapping>
resolveSpccResponseProfileMapping(IGlobalSettingsProvider* settings, const QString& cameraModel,
                                  const QString& filterName, const QString& repositoryId,
                                  const QString& aliasRegistryVersion)
{
    for (const SpccResponseProfileMapping& mapping : loadSpccResponseProfileMappings(settings))
    {
        if (spcc_response_detail::mappingMatches(mapping, cameraModel, filterName, repositoryId, aliasRegistryVersion))
        {
            return mapping;
        }
    }
    return std::nullopt;
}

inline bool saveSpccResponseProfileMapping(IGlobalSettingsProvider* settings, const QString& cameraModel,
                                           const QString& filterName, const QString& repositoryId,
                                           const QString& aliasRegistryVersion, const QString& profileId,
                                           const QString& whiteReferenceId = QString())
{
    if (!settings || cameraModel.trimmed().isEmpty() || profileId.trimmed().isEmpty() ||
        repositoryId.trimmed().isEmpty() || aliasRegistryVersion.trimmed().isEmpty())
    {
        return false;
    }

    QVector<SpccResponseProfileMapping> mappings = loadSpccResponseProfileMappings(settings);
    const SpccResponseProfileMapping nextMapping{
        .cameraModel = cameraModel.trimmed(),
        .filterName = filterName.trimmed(),
        .repositoryId = repositoryId.trimmed(),
        .aliasRegistryVersion = aliasRegistryVersion.trimmed(),
        .profileId = profileId.trimmed(),
        .whiteReferenceId = whiteReferenceId.trimmed(),
    };

    bool replaced = false;
    for (SpccResponseProfileMapping& mapping : mappings)
    {
        if (spcc_response_detail::mappingMatches(mapping, nextMapping.cameraModel, nextMapping.filterName,
                                                 nextMapping.repositoryId, nextMapping.aliasRegistryVersion))
        {
            mapping = nextMapping;
            replaced = true;
            break;
        }
    }
    if (!replaced)
    {
        mappings.push_back(nextMapping);
    }

    QJsonArray array;
    for (const SpccResponseProfileMapping& mapping : mappings)
    {
        QJsonObject object;
        object.insert(QStringLiteral("camera_model"), mapping.cameraModel);
        object.insert(QStringLiteral("filter"), mapping.filterName);
        object.insert(QStringLiteral("repository_id"), mapping.repositoryId);
        object.insert(QStringLiteral("alias_registry_version"), mapping.aliasRegistryVersion);
        object.insert(QStringLiteral("profile_id"), mapping.profileId);
        if (!mapping.whiteReferenceId.isEmpty())
        {
            object.insert(QStringLiteral("white_reference_id"), mapping.whiteReferenceId);
        }
        array.append(object);
    }

    return settings->upsert(SettingsKeys::CATALOGS_SPCC_RESPONSE_PROFILE_MAPPINGS,
                            QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

[[nodiscard]] inline SpccResponseRepositoryInspection
inspectSpccResponseRepositoryDetailed(const QString& directoryPath)
{
    static const QString kResponseMetaFileName = QStringLiteral("response_meta.json");

    SpccResponseRepositoryInspection inspection;
    inspection.directoryPath = directoryPath.trimmed();
    if (inspection.directoryPath.isEmpty())
    {
        inspection.diagnostic = QObject::tr("No SPCC response repository directory is configured.");
        return inspection;
    }

    const QFileInfo directoryInfo(inspection.directoryPath);
    if (!directoryInfo.exists())
    {
        inspection.diagnostic = QObject::tr("SPCC response repository directory does not exist.");
        return inspection;
    }
    if (!directoryInfo.isDir())
    {
        inspection.status = SpccResponseRepositoryStatus::Invalid;
        inspection.diagnostic = QObject::tr("SPCC response repository path is not a directory.");
        return inspection;
    }

    const QDir repositoryDir(inspection.directoryPath);
    const QString metaPath = repositoryDir.filePath(kResponseMetaFileName);
    if (!QFileInfo::exists(metaPath))
    {
        const QFileInfoList entries = repositoryDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
        inspection.status =
            entries.isEmpty() ? SpccResponseRepositoryStatus::Missing : SpccResponseRepositoryStatus::Invalid;
        inspection.diagnostic = entries.isEmpty() ? QObject::tr("SPCC response repository is not installed.")
                                                  : QObject::tr("Missing response_meta.json.");
        return inspection;
    }

    bool metaOk = false;
    const QJsonDocument metaDoc = spcc_response_detail::readJsonFile(metaPath, &metaOk);
    if (!metaOk)
    {
        inspection.status = SpccResponseRepositoryStatus::Invalid;
        inspection.diagnostic = QObject::tr("response_meta.json is not valid JSON.");
        return inspection;
    }

    const QJsonObject meta = metaDoc.object();
    if (meta.value(QStringLiteral("schema_version")).toString() != QStringLiteral("spcc-response-repository-v1"))
    {
        inspection.status = SpccResponseRepositoryStatus::Invalid;
        inspection.diagnostic = QObject::tr("Unsupported SPCC response repository schema.");
        return inspection;
    }

    inspection.repositoryId = meta.value(QStringLiteral("repository_id")).toString().trimmed();
    inspection.version = meta.value(QStringLiteral("version")).toString().trimmed();
    inspection.rightsReviewStatus =
        meta.value(QStringLiteral("rights")).toObject().value(QStringLiteral("review_status")).toString().trimmed();
    if (inspection.repositoryId.isEmpty() || inspection.version.isEmpty())
    {
        inspection.status = SpccResponseRepositoryStatus::Invalid;
        inspection.diagnostic = QObject::tr("SPCC response repository metadata is missing repository identity.");
        return inspection;
    }

    const QJsonArray components = meta.value(QStringLiteral("components")).toArray();
    const QJsonArray whiteReferences = meta.value(QStringLiteral("white_references")).toArray();
    const QJsonArray throughputProfiles = meta.value(QStringLiteral("throughput_profiles")).toArray();
    inspection.componentCount = components.size();
    inspection.whiteReferenceCount = whiteReferences.size();
    inspection.throughputProfileCount = throughputProfiles.size();
    if (components.isEmpty() || whiteReferences.isEmpty() || throughputProfiles.isEmpty())
    {
        inspection.status = SpccResponseRepositoryStatus::Invalid;
        inspection.diagnostic =
            QObject::tr("SPCC response repository must contain components, white references, and profiles.");
        return inspection;
    }

    const QSet<QString> componentIds =
        spcc_response_detail::validateAssets(repositoryDir, components, QStringLiteral("component_id"), inspection);
    if (inspection.status != SpccResponseRepositoryStatus::Missing)
    {
        return inspection;
    }
    const QSet<QString> whiteReferenceIds = spcc_response_detail::validateAssets(
        repositoryDir, whiteReferences, QStringLiteral("white_reference_id"), inspection);
    if (inspection.status != SpccResponseRepositoryStatus::Missing)
    {
        return inspection;
    }

    for (const QJsonValue& profileValue : throughputProfiles)
    {
        if (!profileValue.isObject())
        {
            inspection.status = SpccResponseRepositoryStatus::Invalid;
            inspection.diagnostic = QObject::tr("SPCC response profile entry is not an object.");
            return inspection;
        }

        const QJsonObject profile = profileValue.toObject();
        const QString profileId = profile.value(QStringLiteral("profile_id")).toString().trimmed();
        if (profileId.isEmpty())
        {
            inspection.status = SpccResponseRepositoryStatus::Invalid;
            inspection.diagnostic = QObject::tr("SPCC response profile is missing profile_id.");
            return inspection;
        }

        for (const QJsonValue& componentValue : profile.value(QStringLiteral("component_ids")).toArray())
        {
            const QString componentId = componentValue.toString().trimmed();
            if (!componentIds.contains(componentId))
            {
                inspection.status = SpccResponseRepositoryStatus::Invalid;
                inspection.diagnostic =
                    QObject::tr("SPCC response profile references a missing component: %1").arg(componentId);
                return inspection;
            }
        }

        const QString whiteReferenceId = profile.value(QStringLiteral("white_reference_id")).toString().trimmed();
        if (!whiteReferenceIds.contains(whiteReferenceId))
        {
            inspection.status = SpccResponseRepositoryStatus::Invalid;
            inspection.diagnostic =
                QObject::tr("SPCC response profile references a missing white reference: %1").arg(whiteReferenceId);
            return inspection;
        }

        if (profile.value(QStringLiteral("compatible_channel_bases")).toArray().isEmpty())
        {
            inspection.status = SpccResponseRepositoryStatus::Invalid;
            inspection.diagnostic =
                QObject::tr("SPCC response profile has no compatible channel bases: %1").arg(profileId);
            return inspection;
        }
    }

    inspection.hasRuntimeDeploymentScope =
        spcc_response_detail::hasRuntimeScope(meta.value(QStringLiteral("deployment_scopes")).toArray());
    if (!inspection.hasRuntimeDeploymentScope)
    {
        inspection.status = SpccResponseRepositoryStatus::Invalid;
        inspection.diagnostic =
            QObject::tr("SPCC response repository is not marked for deployed-runtime or user-private use.");
        return inspection;
    }

    inspection.status = SpccResponseRepositoryStatus::Ready;
    inspection.diagnostic = QObject::tr("SPCC response repository is ready.");
    return inspection;
}

[[nodiscard]] inline SpccResponseRepositoryStatus inspectSpccResponseRepository(const QString& directoryPath)
{
    return inspectSpccResponseRepositoryDetailed(directoryPath).status;
}

[[nodiscard]] inline bool isSpccResponseRepositoryReady(const QString& directoryPath)
{
    return inspectSpccResponseRepository(directoryPath) == SpccResponseRepositoryStatus::Ready;
}

[[nodiscard]] inline QString resolveSpccResponseRepositoryDirectory(GlobalSettingsPersistence* settings)
{
    if (!settings)
    {
        return defaultSpccResponseRepositoryDirectory();
    }

    const QString configuredDirectory =
        settings
            ->select(SettingsKeys::CATALOGS_SPCC_RESPONSE_REPOSITORY_DIRECTORY,
                     QString(SettingsDefaults::CATALOGS_SPCC_RESPONSE_REPOSITORY_DIRECTORY))
            .trimmed();
    return configuredDirectory.isEmpty() ? defaultSpccResponseRepositoryDirectory() : configuredDirectory;
}

[[nodiscard]] inline QString resolveSpccResponseRepositoryDirectory(IGlobalSettingsProvider* settings)
{
    if (!settings)
    {
        return defaultSpccResponseRepositoryDirectory();
    }

    const QString configuredDirectory =
        settings
            ->select(SettingsKeys::CATALOGS_SPCC_RESPONSE_REPOSITORY_DIRECTORY,
                     QString(SettingsDefaults::CATALOGS_SPCC_RESPONSE_REPOSITORY_DIRECTORY))
            .trimmed();
    return configuredDirectory.isEmpty() ? defaultSpccResponseRepositoryDirectory() : configuredDirectory;
}

[[nodiscard]] inline SpccResponseRepositoryStatus inspectSpccResponseRepository(GlobalSettingsPersistence* settings)
{
    return inspectSpccResponseRepository(resolveSpccResponseRepositoryDirectory(settings));
}

[[nodiscard]] inline SpccResponseRepositoryStatus inspectSpccResponseRepository(IGlobalSettingsProvider* settings)
{
    return inspectSpccResponseRepository(resolveSpccResponseRepositoryDirectory(settings));
}

} // namespace an::ui

Q_DECLARE_METATYPE(an::ui::SpccResponseRepositoryDescriptor)
