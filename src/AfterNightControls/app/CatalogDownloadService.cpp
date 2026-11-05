// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/CatalogDownloadService.h"

#include "app/AppContextHelpers.h"
#include "app/ApplicationStorage.h"
#include "app/SpccCatalogSettings.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightPlatform/Logging.h>
#include <AfterNightPlatform/Sha256.h>

#include <AfterNightEngine/Algorithms/Catalog/SpectrumCatalog.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/SkyCoordinate.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QSet>
#include <QTimer>
#include <verdigris/wobjectimpl.h>

#include <cctype>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <utility>

namespace
{

constexpr auto kChunkDistributionType = "chunks";
constexpr auto kChunkStorage = "chunks";
constexpr auto kExpectedFormatVersion = 3;
constexpr auto kInstallCompleteMarker = ".install_complete";
constexpr auto kManifestFileName = "catalog_meta.json";
constexpr auto kSpccCatalogMetaSchemaVersion = "spcc-catalog-meta-v1";
constexpr auto kSpccResponseManifestFileName = "response_meta.json";
constexpr auto kSpccResponseRepositorySchemaVersion = "spcc-response-repository-v1";
constexpr int kMaxConcurrentChunkDownloads = 8;
constexpr qint64 kVerificationReadChunkBytes = 4 * 1024 * 1024;

struct ManifestChunk
{
    int index = -1;
    QString file;
    QUrl url;
    qint64 sizeBytes = 0;
    QByteArray sha256Hex;
    bool requireSizeMatch = true;
    bool spccDataChunk = false;
    int spccChunkIndex = -1;
    int firstTile = -1;
    int tileCount = 0;
};

struct ParsedManifest
{
    QString catalogId;
    QString catalogFamilyId;
    QString accessProfile;
    QString rightsReviewStatus;
    QString storage;
    int formatVersion = 0;
    int healpixOrder = an::kXpCatalogHealpixOrder;
    int chunkParentHealpixOrder = an::kXpChunkParentHealpixOrder;
    QVector<ManifestChunk> files;
    qint64 totalBytes = 0;
};

struct ActiveChunkTransfer
{
    int chunkIndex = -1;
    ManifestChunk chunk;
    QPointer<QNetworkReply> reply;
    QFile file;
    QString finalPath;
    QString partPath;
    QString error;
    qint64 bytesReceived = 0;
};

enum class LocalChunkState
{
    Missing,
    NeedsRedownload,
    Verified,
    Error,
};

[[nodiscard]] QByteArray sha256HexForFile(const QString& path)
{
    const std::optional<std::array<std::uint8_t, 32>> digest =
        an::platform::sha256OfFile(std::filesystem::path(path.toStdWString()));
    return digest.has_value() ? QByteArray::fromStdString(an::platform::sha256Hex(*digest)) : QByteArray();
}

[[nodiscard]] bool isSafeRelativePath(const QString& path)
{
    if (path.trimmed().isEmpty())
    {
        return false;
    }

    const QString cleaned = QDir::cleanPath(path);
    if (cleaned.isEmpty() || cleaned == QStringLiteral(".") || cleaned == QStringLiteral(".."))
    {
        return false;
    }
    if (QDir::isAbsolutePath(cleaned))
    {
        return false;
    }
    const QUrl asUrl(cleaned);
    if (!asUrl.scheme().isEmpty() || !asUrl.authority().isEmpty() || !asUrl.fragment().isEmpty() ||
        !asUrl.query().isEmpty())
    {
        return false;
    }
    if (cleaned.startsWith(QStringLiteral("//")) || cleaned.contains(QLatin1Char('\\')))
    {
        return false;
    }
    if (cleaned.startsWith(QStringLiteral("../")) || cleaned.contains(QStringLiteral("/../")))
    {
        return false;
    }
    return true;
}

[[nodiscard]] bool isSafePathSegment(const QString& value)
{
    const QString trimmed = value.trimmed();
    return isSafeRelativePath(trimmed) && !trimmed.contains(QLatin1Char('/')) && !trimmed.contains(QLatin1Char('\\'));
}

[[nodiscard]] bool isValidSha256Hex(const QByteArray& hash)
{
    if (hash.size() != 64)
    {
        return false;
    }

    for (const char value : hash)
    {
        if (!std::isxdigit(static_cast<unsigned char>(value)))
        {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool isApprovedSpccCatalogRightsReviewStatus(const QString& rightsReviewStatus)
{
    const QString normalized = rightsReviewStatus.trimmed();
    return normalized.compare(QStringLiteral("approved-public"), Qt::CaseInsensitive) == 0 ||
           normalized.compare(QStringLiteral("approved-for-managed-download"), Qt::CaseInsensitive) == 0 ||
           normalized.compare(QStringLiteral("approved-for-bundling"), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] bool isHuggingFaceUrl(const QUrl& url)
{
    const QString host = url.host().toLower();
    return host == QStringLiteral("huggingface.co") || host.endsWith(QStringLiteral(".huggingface.co")) ||
           host == QStringLiteral("hf.co") || host.endsWith(QStringLiteral(".hf.co"));
}

[[nodiscard]] QByteArray huggingFaceAccessToken()
{
    QByteArray token = qgetenv("HF_TOKEN").trimmed();
    if (token.isEmpty())
    {
        token = qgetenv("HUGGINGFACE_HUB_TOKEN").trimmed();
    }
    return token;
}

void applyCatalogDownloadRequestHeaders(QNetworkRequest& request)
{
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AfterNight Catalog Downloader"));

    const QByteArray token = huggingFaceAccessToken();
    if (!token.isEmpty() && isHuggingFaceUrl(request.url()))
    {
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token);
    }
}

[[nodiscard]] QString partFilePath(const QString& finalPath)
{
    return finalPath + QStringLiteral(".part");
}

[[nodiscard]] bool ensureParentDirectory(const QString& filePath)
{
    return QDir().mkpath(QFileInfo(filePath).dir().path());
}

[[nodiscard]] bool removePath(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists())
    {
        return true;
    }

    if (info.isDir() && !info.isSymLink())
    {
        QDir dir(path);
        return dir.removeRecursively();
    }

    return QFile::remove(path);
}

[[nodiscard]] LocalChunkState inspectLocalChunk(const ManifestChunk& chunk, const QString& path, QString* error)
{
    const QFileInfo info(path);
    if (!info.exists())
    {
        return LocalChunkState::Missing;
    }
    if (!info.isFile())
    {
        if (error)
        {
            *error = QObject::tr("Catalog chunk path is not a file: %1").arg(path);
        }
        return LocalChunkState::Error;
    }
    if (chunk.requireSizeMatch && info.size() != chunk.sizeBytes)
    {
        return LocalChunkState::NeedsRedownload;
    }

    const QByteArray actualHash = sha256HexForFile(path);
    if (actualHash.isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("Failed to compute SHA-256 for %1").arg(path);
        }
        return LocalChunkState::Error;
    }
    if (actualHash == chunk.sha256Hex)
    {
        return LocalChunkState::Verified;
    }

    return LocalChunkState::NeedsRedownload;
}

[[nodiscard]] bool writeInstallMarker(const QString& directoryPath, QString* error)
{
    const QString markerPath = QDir(directoryPath).filePath(QString::fromLatin1(kInstallCompleteMarker));
    QFile marker(markerPath);
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (error)
        {
            *error = QObject::tr("Failed to write install marker: %1").arg(markerPath);
        }
        return false;
    }
    marker.write("ok\n");
    marker.close();
    return true;
}

[[nodiscard]] bool promoteStagingDirectory(const QString& stagingDirectory, const QString& installDirectory,
                                           const QString& backupDirectory, QString* error)
{
    if (!removePath(backupDirectory))
    {
        if (error)
        {
            *error = QObject::tr("Failed to clear old catalog backup directory: %1").arg(backupDirectory);
        }
        return false;
    }

    const bool hadExistingInstall = QFileInfo::exists(installDirectory);
    if (hadExistingInstall && !QDir().rename(installDirectory, backupDirectory))
    {
        if (error)
        {
            *error = QObject::tr("Failed to move existing catalog aside: %1").arg(installDirectory);
        }
        return false;
    }

    if (!QDir().rename(stagingDirectory, installDirectory))
    {
        QString restoreError;
        if (hadExistingInstall && !QDir().rename(backupDirectory, installDirectory))
        {
            restoreError = QObject::tr(" Failed to restore previous catalog from %1.").arg(backupDirectory);
        }

        if (error)
        {
            *error =
                QObject::tr("Failed to promote staged catalog into place: %1.%2").arg(stagingDirectory, restoreError);
        }
        return false;
    }

    if (hadExistingInstall && !removePath(backupDirectory))
    {
        LOG_WARNING("Catalog backup cleanup failed: {}", backupDirectory.toStdString());
    }

    return true;
}

[[nodiscard]] bool appendManifestFile(QVector<ManifestChunk>& files, QSet<QString>& seenFiles, const int index,
                                      const QString& filePath, const QByteArray& sha256Hex, const qint64 sizeBytes,
                                      const bool requireSizeMatch, const QString& label, const QUrl& manifestUrl,
                                      QString* error, const bool spccDataChunk = false, const int firstTile = -1,
                                      const int tileCount = 0, const int spccChunkIndex = -1)
{
    ManifestChunk file;
    file.index = index;
    file.file = QDir::cleanPath(filePath);
    file.sizeBytes = sizeBytes;
    file.sha256Hex = sha256Hex.trimmed().toLower();
    file.requireSizeMatch = requireSizeMatch;
    file.spccDataChunk = spccDataChunk;
    file.spccChunkIndex = spccChunkIndex;
    file.firstTile = firstTile;
    file.tileCount = tileCount;

    if (!isSafeRelativePath(file.file))
    {
        if (error)
        {
            *error = QObject::tr("Catalog manifest contains an invalid %1 path: %2").arg(label, file.file);
        }
        return false;
    }
    if (file.requireSizeMatch && file.sizeBytes <= 0)
    {
        if (error)
        {
            *error = QObject::tr("Catalog manifest contains an invalid %1 size for %2").arg(label, file.file);
        }
        return false;
    }
    if (file.sha256Hex.isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("Catalog manifest is missing a SHA-256 hash for %1").arg(file.file);
        }
        return false;
    }
    if (!isValidSha256Hex(file.sha256Hex))
    {
        if (error)
        {
            *error = QObject::tr("Catalog manifest contains an invalid SHA-256 hash for %1").arg(file.file);
        }
        return false;
    }
    if (seenFiles.contains(file.file))
    {
        if (error)
        {
            *error = QObject::tr("Catalog manifest contains duplicate file paths: %1").arg(file.file);
        }
        return false;
    }

    seenFiles.insert(file.file);
    file.url = manifestUrl.resolved(QUrl(file.file));
    files.push_back(std::move(file));
    return true;
}

[[nodiscard]] bool appendSpccSidecar(QVector<ManifestChunk>& files, QSet<QString>& seenFiles, const int index,
                                     const QJsonObject& fileObject, const QJsonObject& root, const QString& fileKey,
                                     const QString& checksumKey, const QUrl& manifestUrl, QString* error)
{
    const QString relativePath = fileObject.value(fileKey).toString();
    const QByteArray sha256Hex = root.value(checksumKey).toString().toLatin1();
    if (relativePath.trimmed().isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("SPCC catalog metadata is missing sidecar path: %1").arg(fileKey);
        }
        return false;
    }
    return appendManifestFile(files, seenFiles, index, relativePath, sha256Hex, 0, false, fileKey, manifestUrl, error);
}

[[nodiscard]] bool appendOptionalSpccSidecar(QVector<ManifestChunk>& files, QSet<QString>& seenFiles, int& index,
                                             const QJsonObject& fileObject, const QJsonObject& root,
                                             const QString& fileKey, const QString& checksumKey,
                                             const QUrl& manifestUrl, QString* error)
{
    if (!fileObject.contains(fileKey) && !root.contains(checksumKey))
    {
        return true;
    }
    if (!fileObject.contains(fileKey) || !root.contains(checksumKey))
    {
        if (error)
        {
            *error = QObject::tr("SPCC catalog optional sidecar must include both path and checksum: %1").arg(fileKey);
        }
        return false;
    }
    if (!appendSpccSidecar(files, seenFiles, index, fileObject, root, fileKey, checksumKey, manifestUrl, error))
    {
        return false;
    }
    ++index;
    return true;
}

[[nodiscard]] std::optional<ParsedManifest> parseGaiaChunkManifest(const QJsonObject& root, const QUrl& manifestUrl,
                                                                   const QString& expectedCatalogId, QString* error)
{
    ParsedManifest manifest;
    manifest.catalogId = root.value(QStringLiteral("catalog_id")).toString();
    manifest.storage = root.value(QStringLiteral("storage")).toString();
    manifest.formatVersion = root.value(QStringLiteral("format_version")).toInt();

    if (manifest.formatVersion != kExpectedFormatVersion)
    {
        if (error)
        {
            *error = QObject::tr("Unsupported catalog manifest format version: %1").arg(manifest.formatVersion);
        }
        return std::nullopt;
    }
    if (manifest.storage != QString::fromLatin1(kChunkStorage))
    {
        if (error)
        {
            *error = QObject::tr("Unsupported catalog manifest storage mode: %1").arg(manifest.storage);
        }
        return std::nullopt;
    }
    if (!expectedCatalogId.isEmpty() && manifest.catalogId != expectedCatalogId)
    {
        if (error)
        {
            *error = QObject::tr("Catalog manifest ID mismatch: expected %1, got %2")
                         .arg(expectedCatalogId, manifest.catalogId);
        }
        return std::nullopt;
    }

    const QJsonArray chunksArray = root.value(QStringLiteral("chunks")).toArray();
    if (chunksArray.isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("Catalog manifest does not list any chunk files.");
        }
        return std::nullopt;
    }

    const int chunkCount = root.value(QStringLiteral("chunk_count")).toInt(chunksArray.size());
    if (chunkCount != chunksArray.size())
    {
        if (error)
        {
            *error = QObject::tr("Catalog manifest chunk_count does not match the chunk list.");
        }
        return std::nullopt;
    }

    QSet<QString> seenFiles;
    manifest.files.reserve(chunksArray.size());
    for (int i = 0; i < chunksArray.size(); ++i)
    {
        const QJsonObject chunkObject = chunksArray.at(i).toObject();
        const int chunkIndex = chunkObject.value(QStringLiteral("index")).toInt(i);
        if (chunkIndex != i)
        {
            if (error)
            {
                *error = QObject::tr("Catalog manifest chunk indices must be contiguous and zero-based.");
            }
            return std::nullopt;
        }

        const qint64 sizeBytes = static_cast<qint64>(chunkObject.value(QStringLiteral("size_bytes")).toDouble(0.0));
        if (!appendManifestFile(manifest.files, seenFiles, i, chunkObject.value(QStringLiteral("file")).toString(),
                                chunkObject.value(QStringLiteral("sha256")).toString().toLatin1(), sizeBytes, true,
                                QStringLiteral("chunk"), manifestUrl, error))
        {
            return std::nullopt;
        }
        manifest.totalBytes += sizeBytes;
    }

    return manifest;
}

[[nodiscard]] std::optional<ParsedManifest> parseSpccCatalogMeta(const QByteArray& data, const QJsonObject& root,
                                                                 const QUrl& manifestUrl,
                                                                 const an::ui::CatalogPackDescriptor& expectedPack,
                                                                 QString* error)
{
    ParsedManifest manifest;
    manifest.catalogId = root.value(QStringLiteral("manifest_id")).toString();
    manifest.catalogFamilyId = root.value(QStringLiteral("catalog_family_id")).toString().trimmed();
    manifest.accessProfile = root.value(QStringLiteral("access_profile")).toString().trimmed();
    manifest.rightsReviewStatus =
        root.value(QStringLiteral("rights")).toObject().value(QStringLiteral("review_status")).toString().trimmed();
    manifest.storage = QStringLiteral("spcc-anxp");
    manifest.formatVersion = 1;
    const QJsonObject formatObject = root.value(QStringLiteral("format")).toObject();
    manifest.healpixOrder = formatObject.value(QStringLiteral("healpix_order")).toInt(an::kXpCatalogHealpixOrder);
    manifest.chunkParentHealpixOrder =
        formatObject.value(QStringLiteral("chunk_parent_healpix_order")).toInt(an::kXpChunkParentHealpixOrder);

    const QString catalogFamilyId = manifest.catalogFamilyId;
    const QString accessProfile = manifest.accessProfile;
    const QString rightsReviewStatus = manifest.rightsReviewStatus;
    const QString expectedCatalogMetaSha = expectedPack.catalogMetaSha256.trimmed();

    if (expectedCatalogMetaSha.isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("SPCC catalog descriptor is missing its manifest checksum.");
        }
        return std::nullopt;
    }
    const QByteArray actualManifestSha = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    if (QString::fromLatin1(actualManifestSha).compare(expectedCatalogMetaSha, Qt::CaseInsensitive) != 0)
    {
        if (error)
        {
            *error = QObject::tr("SPCC catalog manifest checksum mismatch.");
        }
        return std::nullopt;
    }

    if (manifest.catalogId.trimmed().isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("SPCC catalog metadata is missing manifest_id.");
        }
        return std::nullopt;
    }
    if (!expectedPack.id.isEmpty() && manifest.catalogId != expectedPack.id)
    {
        if (error)
        {
            *error = QObject::tr("SPCC catalog manifest ID mismatch: expected %1, got %2")
                         .arg(expectedPack.id, manifest.catalogId);
        }
        return std::nullopt;
    }
    if (catalogFamilyId.isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("SPCC catalog metadata is missing catalog_family_id.");
        }
        return std::nullopt;
    }
    if (!expectedPack.catalogFamilyId.isEmpty() &&
        catalogFamilyId.compare(expectedPack.catalogFamilyId, Qt::CaseInsensitive) != 0)
    {
        if (error)
        {
            *error = QObject::tr("SPCC catalog family mismatch: expected %1, got %2")
                         .arg(expectedPack.catalogFamilyId, catalogFamilyId);
        }
        return std::nullopt;
    }
    if (accessProfile.isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("SPCC catalog metadata is missing access_profile.");
        }
        return std::nullopt;
    }
    if (!expectedPack.accessProfile.isEmpty() &&
        accessProfile.compare(expectedPack.accessProfile, Qt::CaseInsensitive) != 0)
    {
        if (error)
        {
            *error = QObject::tr("SPCC catalog access profile mismatch: expected %1, got %2")
                         .arg(expectedPack.accessProfile, accessProfile);
        }
        return std::nullopt;
    }
    if (rightsReviewStatus.isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("SPCC catalog metadata is missing rights review status.");
        }
        return std::nullopt;
    }
    if (!isApprovedSpccCatalogRightsReviewStatus(rightsReviewStatus))
    {
        if (error)
        {
            *error = QObject::tr("SPCC catalog rights review status is not approved for managed download: %1")
                         .arg(rightsReviewStatus);
        }
        return std::nullopt;
    }
    if (!expectedPack.rightsReviewStatus.isEmpty() &&
        rightsReviewStatus.compare(expectedPack.rightsReviewStatus, Qt::CaseInsensitive) != 0)
    {
        if (error)
        {
            *error = QObject::tr("SPCC catalog rights review status mismatch: expected %1, got %2")
                         .arg(expectedPack.rightsReviewStatus, rightsReviewStatus);
        }
        return std::nullopt;
    }

    const QJsonObject filesObject = root.value(QStringLiteral("files")).toObject();
    if (filesObject.isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("SPCC catalog metadata does not list required sidecar files.");
        }
        return std::nullopt;
    }

    const QJsonArray chunksArray = root.value(QStringLiteral("chunks")).toArray();
    if (chunksArray.isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("SPCC catalog metadata does not list any chunk files.");
        }
        return std::nullopt;
    }

    QSet<QString> seenFiles;
    manifest.files.reserve(filesObject.size() + chunksArray.size());
    int fileIndex = 0;
    if (!appendSpccSidecar(manifest.files, seenFiles, fileIndex++, filesObject, root, QStringLiteral("chunk_manifest"),
                           QStringLiteral("chunk_manifest_sha256"), manifestUrl, error) ||
        !appendSpccSidecar(manifest.files, seenFiles, fileIndex++, filesObject, root,
                           QStringLiteral("coverage_summary"), QStringLiteral("coverage_summary_sha256"), manifestUrl,
                           error) ||
        !appendSpccSidecar(manifest.files, seenFiles, fileIndex++, filesObject, root, QStringLiteral("source_id_index"),
                           QStringLiteral("source_id_index_sha256"), manifestUrl, error) ||
        !appendSpccSidecar(manifest.files, seenFiles, fileIndex++, filesObject, root,
                           QStringLiteral("source_count_transcript"), QStringLiteral("source_count_transcript_sha256"),
                           manifestUrl, error))
    {
        return std::nullopt;
    }
    if (!appendOptionalSpccSidecar(manifest.files, seenFiles, fileIndex, filesObject, root,
                                   QStringLiteral("regional_cache_request"),
                                   QStringLiteral("regional_cache_request_sha256"), manifestUrl, error))
    {
        return std::nullopt;
    }

    for (int i = 0; i < chunksArray.size(); ++i)
    {
        const QJsonObject chunkObject = chunksArray.at(i).toObject();
        const int chunkIndex = chunkObject.value(QStringLiteral("chunk_index")).toInt(i);
        if (chunkIndex != i)
        {
            if (error)
            {
                *error = QObject::tr("SPCC catalog chunk indices must be contiguous and zero-based.");
            }
            return std::nullopt;
        }

        const qint64 sizeBytes = static_cast<qint64>(chunkObject.value(QStringLiteral("size_bytes")).toDouble(0.0));
        const int firstTile = chunkObject.value(QStringLiteral("first_tile")).toInt(-1);
        const int tileCount = chunkObject.value(QStringLiteral("tile_count")).toInt(0);
        if (!appendManifestFile(manifest.files, seenFiles, fileIndex++,
                                chunkObject.value(QStringLiteral("file")).toString(),
                                chunkObject.value(QStringLiteral("sha256")).toString().toLatin1(), sizeBytes, true,
                                QStringLiteral("chunk"), manifestUrl, error, true, firstTile, tileCount, chunkIndex))
        {
            return std::nullopt;
        }
        manifest.totalBytes += sizeBytes;
    }

    return manifest;
}

[[nodiscard]] bool responseManifestHasDeploymentScope(const QJsonObject& root, const QString& deploymentScope)
{
    const QJsonArray scopes = root.value(QStringLiteral("deployment_scopes")).toArray();
    for (const QJsonValue& scopeValue : scopes)
    {
        if (scopeValue.toString().compare(deploymentScope, Qt::CaseInsensitive) == 0)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool appendSpccResponseAsset(QVector<ManifestChunk>& files, QSet<QString>& seenFiles, int& index,
                                           const QJsonObject& assetObject, const QString& idKey, const QString& label,
                                           const QUrl& manifestUrl, QString* error)
{
    const QString assetId = assetObject.value(idKey).toString().trimmed();
    const QString relativeFile = assetObject.value(QStringLiteral("file")).toString().trimmed();
    const QByteArray sha256Hex = assetObject.value(QStringLiteral("file_sha256")).toString().toLatin1();
    if (assetId.isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("SPCC response repository metadata contains an asset without %1.").arg(idKey);
        }
        return false;
    }
    if (relativeFile.isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("SPCC response repository asset is missing file path: %1").arg(assetId);
        }
        return false;
    }
    if (assetObject.value(QStringLiteral("data_sha256")).toString().trimmed().isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("SPCC response repository asset is missing data checksum: %1").arg(assetId);
        }
        return false;
    }
    const QString reviewStatus = assetObject.value(QStringLiteral("review_status")).toString().trimmed();
    if (!an::ui::isApprovedSpccResponseDescriptorRightsStatus(reviewStatus))
    {
        if (error)
        {
            *error = QObject::tr("SPCC response repository asset review status is not approved: %1 (%2)")
                         .arg(assetId, reviewStatus);
        }
        return false;
    }
    return appendManifestFile(files, seenFiles, index++, relativeFile, sha256Hex, 0, false, label, manifestUrl, error);
}

[[nodiscard]] std::optional<ParsedManifest>
parseSpccResponseRepositoryMeta(const QByteArray& data, const QJsonObject& root, const QUrl& manifestUrl,
                                const an::ui::SpccResponseRepositoryDescriptor& expectedRepository, QString* error)
{
    ParsedManifest manifest;
    manifest.catalogId = root.value(QStringLiteral("repository_id")).toString().trimmed();
    manifest.storage = QStringLiteral("spcc-response-repository");
    manifest.formatVersion = 1;

    if (!expectedRepository.responseMetaSha256.trimmed().isEmpty())
    {
        const QByteArray actualManifestSha = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
        if (QString::fromLatin1(actualManifestSha)
                .compare(expectedRepository.responseMetaSha256.trimmed(), Qt::CaseInsensitive) != 0)
        {
            if (error)
            {
                *error = QObject::tr("SPCC response repository manifest checksum mismatch.");
            }
            return std::nullopt;
        }
    }
    if (root.value(QStringLiteral("schema_version")).toString() !=
        QString::fromLatin1(kSpccResponseRepositorySchemaVersion))
    {
        if (error)
        {
            *error = QObject::tr("Unsupported SPCC response repository schema.");
        }
        return std::nullopt;
    }
    if (manifest.catalogId.isEmpty() || root.value(QStringLiteral("version")).toString().trimmed().isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("SPCC response repository metadata is missing repository identity.");
        }
        return std::nullopt;
    }
    if (!expectedRepository.repositoryId.isEmpty() &&
        manifest.catalogId.compare(expectedRepository.repositoryId, Qt::CaseInsensitive) != 0)
    {
        if (error)
        {
            *error = QObject::tr("SPCC response repository ID mismatch: expected %1, got %2")
                         .arg(expectedRepository.repositoryId, manifest.catalogId);
        }
        return std::nullopt;
    }

    const QString version = root.value(QStringLiteral("version")).toString().trimmed();
    if (!expectedRepository.version.isEmpty() && version.compare(expectedRepository.version, Qt::CaseInsensitive) != 0)
    {
        if (error)
        {
            *error = QObject::tr("SPCC response repository version mismatch: expected %1, got %2")
                         .arg(expectedRepository.version, version);
        }
        return std::nullopt;
    }

    const QString rightsReviewStatus =
        root.value(QStringLiteral("rights")).toObject().value(QStringLiteral("review_status")).toString().trimmed();
    if (rightsReviewStatus.isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("SPCC response repository metadata is missing rights review status.");
        }
        return std::nullopt;
    }
    if (!an::ui::isApprovedSpccResponseDescriptorRightsStatus(rightsReviewStatus))
    {
        if (error)
        {
            *error = QObject::tr("SPCC response repository rights review status is not approved: %1")
                         .arg(rightsReviewStatus);
        }
        return std::nullopt;
    }
    if (!expectedRepository.rightsReviewStatus.isEmpty() &&
        rightsReviewStatus.compare(expectedRepository.rightsReviewStatus, Qt::CaseInsensitive) != 0)
    {
        if (error)
        {
            *error = QObject::tr("SPCC response repository rights review status mismatch: expected %1, got %2")
                         .arg(expectedRepository.rightsReviewStatus, rightsReviewStatus);
        }
        return std::nullopt;
    }
    if (!expectedRepository.deploymentScope.isEmpty() &&
        !responseManifestHasDeploymentScope(root, expectedRepository.deploymentScope))
    {
        if (error)
        {
            *error = QObject::tr("SPCC response repository does not declare deployment scope: %1")
                         .arg(expectedRepository.deploymentScope);
        }
        return std::nullopt;
    }

    const QJsonArray components = root.value(QStringLiteral("components")).toArray();
    const QJsonArray whiteReferences = root.value(QStringLiteral("white_references")).toArray();
    const QJsonArray throughputProfiles = root.value(QStringLiteral("throughput_profiles")).toArray();
    if (components.isEmpty() || whiteReferences.isEmpty() || throughputProfiles.isEmpty())
    {
        if (error)
        {
            *error = QObject::tr("SPCC response repository must contain components, white references, and profiles.");
        }
        return std::nullopt;
    }

    QSet<QString> seenFiles;
    int fileIndex = 0;
    manifest.files.reserve(components.size() + whiteReferences.size());
    for (const QJsonValue& componentValue : components)
    {
        if (!componentValue.isObject() ||
            !appendSpccResponseAsset(manifest.files, seenFiles, fileIndex, componentValue.toObject(),
                                     QStringLiteral("component_id"), QStringLiteral("component"), manifestUrl, error))
        {
            return std::nullopt;
        }
    }
    for (const QJsonValue& whiteReferenceValue : whiteReferences)
    {
        if (!whiteReferenceValue.isObject() ||
            !appendSpccResponseAsset(manifest.files, seenFiles, fileIndex, whiteReferenceValue.toObject(),
                                     QStringLiteral("white_reference_id"), QStringLiteral("white reference"),
                                     manifestUrl, error))
        {
            return std::nullopt;
        }
    }

    manifest.totalBytes = expectedRepository.downloadSizeBytes;
    return manifest;
}

[[nodiscard]] std::optional<ParsedManifest> parseManifestBytes(const QByteArray& data, const QUrl& manifestUrl,
                                                               const an::ui::CatalogPackDescriptor& expectedPack,
                                                               QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (error)
        {
            *error = QObject::tr("Failed to parse catalog manifest: %1").arg(parseError.errorString());
        }
        return std::nullopt;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("schema_version")).toString() == QString::fromLatin1(kSpccCatalogMetaSchemaVersion))
    {
        return parseSpccCatalogMeta(data, root, manifestUrl, expectedPack, error);
    }

    return parseGaiaChunkManifest(root, manifestUrl, expectedPack.id, error);
}

[[nodiscard]] bool isSpccParsedManifest(const ParsedManifest& manifest)
{
    return manifest.storage == QStringLiteral("spcc-anxp");
}

/// The engine's manifest of a parsed SPCC catalog: the access profile and each data chunk's declared tile range,
/// which is what its chunk selection reads.
[[nodiscard]] an::SpectrumCatalogManifest spccManifestFromParsedManifest(const ParsedManifest& manifest)
{
    an::SpectrumCatalogManifest engineManifest;
    engineManifest.catalogFamilyId = manifest.catalogFamilyId.toStdString();
    engineManifest.manifestId = manifest.catalogId.toStdString();
    engineManifest.accessProfile =
        manifest.accessProfile.compare(QStringLiteral("local-full"), Qt::CaseInsensitive) == 0
            ? an::SpectrumCatalogAccessProfile::LocalFullInstall
            : an::SpectrumCatalogAccessProfile::InstalledRegionalCache;
    engineManifest.rightsReviewStatus = manifest.rightsReviewStatus.toStdString();
    engineManifest.healpixOrder = static_cast<std::uint8_t>(manifest.healpixOrder);
    engineManifest.chunkParentHealpixOrder = static_cast<std::uint8_t>(manifest.chunkParentHealpixOrder);
    for (const ManifestChunk& file : manifest.files)
    {
        if (!file.spccDataChunk)
        {
            continue;
        }

        an::SpectrumChunkDescriptor chunk;
        chunk.chunkIndex = file.spccChunkIndex;
        chunk.file = file.file.toStdString();
        chunk.firstTile = file.firstTile;
        chunk.tileCount = file.tileCount;
        chunk.sizeBytes = file.sizeBytes > 0 ? static_cast<std::uint64_t>(file.sizeBytes) : 0U;
        chunk.sha256 = file.sha256Hex.toStdString();
        engineManifest.chunks.push_back(std::move(chunk));
    }
    return engineManifest;
}

/// Whether a chunk request names only some of a catalog's chunks, which the engine selects on a worker.
[[nodiscard]] bool needsSpccChunkSelection(const ParsedManifest& manifest,
                                           const an::ui::SpccCatalogChunkDownloadRequest& request)
{
    return isSpccParsedManifest(manifest) && request.mode != an::ui::SpccCatalogChunkDownloadMode::FullCatalog;
}

/// The data chunks a request's region and Gaia sources need, as the engine selects them: the chunks whose declared
/// tile ranges meet the region's disc, and the chunks the sources' own tiles name.
[[nodiscard]] an::Result<QSet<int>> selectRequiredSpccChunks(const an::ExecutionContext& execution,
                                                             const an::SpectrumCatalogManifest& manifest,
                                                             const an::ui::SpccCatalogChunkDownloadRequest& request)
{
    if (!request.hasRegion && request.sourceIds.empty())
    {
        return an::Status::failure(
            an::ErrorCode::InvalidArgument,
            an::StatusDetail{.message =
                                 QObject::tr("SPCC catalog chunk request does not include a WCS region or source "
                                             "IDs.")
                                     .toStdString()});
    }

    QSet<int> requiredChunks;
    if (request.hasRegion)
    {
        const an::Result<an::SpectrumChunkSelection> region = an::selectSpectrumChunksForRegion(
            execution, manifest,
            an::SkyCoordinate{.rightAscensionDegrees = request.centerRaDeg, .declinationDegrees = request.centerDecDeg},
            request.radiusDeg);
        if (!region.hasValue())
        {
            return region.status();
        }
        for (const std::uint32_t chunkIndex : region->chunkIndices)
        {
            requiredChunks.insert(static_cast<int>(chunkIndex));
        }
    }
    const an::Result<std::vector<std::uint32_t>> sources =
        an::selectSpectrumChunksForSources(execution, manifest, request.sourceIds);
    if (!sources.hasValue())
    {
        return sources.status();
    }
    for (const std::uint32_t chunkIndex : *sources)
    {
        requiredChunks.insert(static_cast<int>(chunkIndex));
    }
    if (requiredChunks.isEmpty())
    {
        return an::Status::failure(
            an::ErrorCode::EmptySelection,
            an::StatusDetail{.message = QObject::tr("SPCC catalog metadata does not declare chunks for the requested "
                                                    "target region.")
                                            .toStdString()});
    }
    return requiredChunks;
}

/// Keeps the data chunks a request needs and every file that is not a data chunk.
void keepRequiredSpccChunks(ParsedManifest& manifest, const QSet<int>& requiredChunks)
{
    QVector<ManifestChunk> filteredFiles;
    filteredFiles.reserve(manifest.files.size());
    qint64 filteredBytes = 0;
    for (const ManifestChunk& file : manifest.files)
    {
        if (file.spccDataChunk && !requiredChunks.contains(file.spccChunkIndex))
        {
            continue;
        }
        filteredBytes += file.sizeBytes;
        filteredFiles.push_back(file);
    }
    manifest.files = std::move(filteredFiles);
    manifest.totalBytes = filteredBytes;
}

[[nodiscard]] QString defaultCatalogRootDirectory()
{
    const QString base = an::ui::applicationLocalDataPath();
    return QDir(base).filePath(QStringLiteral("catalogs"));
}

} // namespace

namespace an::ui
{

struct CatalogDownloadService::impl
{
    enum class Phase
    {
        Idle,
        DownloadingManifest,
        DownloadingChunk,
        VerifyingChunk,
    };

    enum class TransferKind
    {
        None,
        Manifest,
        Chunk,
    };

    enum class DownloadMode
    {
        Catalog,
        SpccCatalogChunks,
        SpccResponseRepository,
    };

    QNetworkAccessManager networkManager;
    QPointer<QNetworkReply> activeReply;
    std::map<QNetworkReply*, std::unique_ptr<ActiveChunkTransfer>> activeChunkTransfers;
    std::deque<std::unique_ptr<ActiveChunkTransfer>> pendingChunkVerifications;
    std::unique_ptr<ActiveChunkTransfer> verifyingChunk;
    CatalogPackDescriptor activePack;
    SpccResponseRepositoryDescriptor activeResponseRepository;
    QString catalogRootDirectory = ::defaultCatalogRootDirectory();
    QString spccResponseRepositoryRootDirectory = an::ui::defaultSpccResponseRepositoryDirectory();
    QFile activeFile;
    QFile verifyingFile;
    std::unique_ptr<QCryptographicHash> verifyingHash;
    ParsedManifest manifest;
    SpccCatalogChunkDownloadRequest activeSpccChunkRequest;
    ManifestChunk activeChunk;
    QString activeFinalPath;
    QString activePartPath;
    QString transferError;
    int nextChunkIndex = 0;
    qint64 verifiedBytes = 0;
    qint64 verifyingBytesRead = 0;
    int verifiedChunks = 0;
    qint64 lastCatalogProgressBytes = 0;
    bool cancelled = false;
    Phase phase = Phase::Idle;
    TransferKind transferKind = TransferKind::None;
    DownloadMode downloadMode = DownloadMode::Catalog;
    bool useIncrementalCatalogInstall = false;

    /// Moves when a chunk selection starts and when a download finishes, so a selection that lands for a download
    /// that has ended is dropped.
    std::uint64_t spccSelectionTicket = 0u;

    [[nodiscard]] qint64 activeChunkBytesReceived() const
    {
        qint64 bytesReceived = 0;
        for (const auto& [reply, transfer] : activeChunkTransfers)
        {
            (void)reply;
            if (transfer)
            {
                bytesReceived += transfer->bytesReceived;
            }
        }
        return bytesReceived;
    }

    void abortActiveChunkTransfers()
    {
        QVector<QPointer<QNetworkReply>> repliesToAbort;
        repliesToAbort.reserve(static_cast<int>(activeChunkTransfers.size()));
        for (auto& [reply, transfer] : activeChunkTransfers)
        {
            if (transfer && transfer->file.isOpen())
            {
                transfer->file.close();
            }
            if (reply)
            {
                repliesToAbort.push_back(QPointer<QNetworkReply>(reply));
            }
        }

        for (const QPointer<QNetworkReply>& reply : repliesToAbort)
        {
            if (phase == Phase::Idle)
            {
                break;
            }
            if (reply)
            {
                reply->abort();
            }
        }
    }

    void clearActiveChunkTransfers()
    {
        for (auto& [reply, transfer] : activeChunkTransfers)
        {
            if (transfer && transfer->file.isOpen())
            {
                transfer->file.close();
            }
            if (transfer && !transfer->partPath.isEmpty())
            {
                (void)removePath(transfer->partPath);
            }
            if (reply)
            {
                reply->deleteLater();
            }
        }
        activeChunkTransfers.clear();
    }

    void clearChunkVerifications()
    {
        if (verifyingFile.isOpen())
        {
            verifyingFile.close();
        }
        verifyingHash.reset();
        verifyingBytesRead = 0;

        if (verifyingChunk && !verifyingChunk->partPath.isEmpty())
        {
            (void)removePath(verifyingChunk->partPath);
        }
        verifyingChunk.reset();

        for (auto& transfer : pendingChunkVerifications)
        {
            if (transfer && !transfer->partPath.isEmpty())
            {
                (void)removePath(transfer->partPath);
            }
        }
        pendingChunkVerifications.clear();
    }

    [[nodiscard]] QString manifestFileName() const
    {
        return downloadMode == DownloadMode::SpccResponseRepository ? QString::fromLatin1(kSpccResponseManifestFileName)
                                                                    : QString::fromLatin1(kManifestFileName);
    }

    [[nodiscard]] QString installDirectory() const
    {
        if (downloadMode == DownloadMode::SpccResponseRepository)
        {
            return QDir(QDir(spccResponseRepositoryRootDirectory).filePath(activeResponseRepository.repositoryId))
                .filePath(activeResponseRepository.version);
        }
        return QDir(catalogRootDirectory).filePath(activePack.id);
    }

    [[nodiscard]] QString stagingDirectory() const
    {
        if (downloadMode == DownloadMode::SpccResponseRepository)
        {
            return QDir(QDir(spccResponseRepositoryRootDirectory).filePath(activeResponseRepository.repositoryId))
                .filePath(activeResponseRepository.version + QStringLiteral(".installing"));
        }
        return QDir(catalogRootDirectory).filePath(activePack.id + QStringLiteral(".installing"));
    }

    [[nodiscard]] QString backupDirectory() const
    {
        if (downloadMode == DownloadMode::SpccResponseRepository)
        {
            return QDir(QDir(spccResponseRepositoryRootDirectory).filePath(activeResponseRepository.repositoryId))
                .filePath(activeResponseRepository.version + QStringLiteral(".backup"));
        }
        return QDir(catalogRootDirectory).filePath(activePack.id + QStringLiteral(".backup"));
    }

    [[nodiscard]] QString manifestPath() const
    {
        return QDir(useIncrementalCatalogInstall ? installDirectory() : stagingDirectory())
            .filePath(manifestFileName());
    }

    [[nodiscard]] QString transferDirectory() const
    {
        return useIncrementalCatalogInstall ? installDirectory() : stagingDirectory();
    }
};

W_OBJECT_IMPL(CatalogDownloadService)

CatalogDownloadService::CatalogDownloadService(QObject* parent) : QObject(parent), m_impl(std::make_unique<impl>())
{
    qRegisterMetaType<an::ui::CatalogPackDescriptor>("an::ui::CatalogPackDescriptor");
    qRegisterMetaType<an::ui::SpccResponseRepositoryDescriptor>("an::ui::SpccResponseRepositoryDescriptor");
    qRegisterMetaType<an::ui::CatalogDownloadProgress>("an::ui::CatalogDownloadProgress");
    qRegisterMetaType<an::ui::CatalogDownloadResult>("an::ui::CatalogDownloadResult");
    m_impl->networkManager.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

CatalogDownloadService::~CatalogDownloadService()
{
    cancel();
}

QString CatalogDownloadService::defaultCatalogRootDirectory()
{
    return ::defaultCatalogRootDirectory();
}

void CatalogDownloadService::setCatalogRootDirectory(const QString& path)
{
    if (isBusy())
    {
        return;
    }

    m_impl->catalogRootDirectory = path.trimmed().isEmpty() ? defaultCatalogRootDirectory() : path.trimmed();
}

QString CatalogDownloadService::catalogRootDirectory() const
{
    return m_impl->catalogRootDirectory;
}

void CatalogDownloadService::setSpccResponseRepositoryRootDirectory(const QString& path)
{
    if (isBusy())
    {
        return;
    }

    m_impl->spccResponseRepositoryRootDirectory =
        path.trimmed().isEmpty() ? an::ui::defaultSpccResponseRepositoryDirectory() : path.trimmed();
}

QString CatalogDownloadService::spccResponseRepositoryRootDirectory() const
{
    return m_impl->spccResponseRepositoryRootDirectory;
}

bool CatalogDownloadService::isBusy() const
{
    return m_impl->phase != impl::Phase::Idle;
}

void CatalogDownloadService::downloadCatalogPack(const CatalogPackDescriptor& pack)
{
    if (isBusy())
    {
        return;
    }

    if (!isUsableCatalogManifestUrl(pack.manifestUrl))
    {
        CatalogDownloadResult result;
        result.packId = pack.id;
        result.errorMessage = tr("Catalog pack has no manifest URL configured.");
        emit downloadFinished(result);
        return;
    }
    if (pack.distributionType.trimmed().compare(QString::fromLatin1(kChunkDistributionType), Qt::CaseInsensitive) != 0)
    {
        CatalogDownloadResult result;
        result.packId = pack.id;
        result.errorMessage = tr("Unsupported catalog distribution type: %1").arg(pack.distributionType);
        emit downloadFinished(result);
        return;
    }
    if (!isSafePathSegment(pack.id))
    {
        CatalogDownloadResult result;
        result.packId = pack.id;
        result.errorMessage = tr("Catalog pack has an unsafe ID.");
        emit downloadFinished(result);
        return;
    }

    if (!QDir().mkpath(m_impl->catalogRootDirectory))
    {
        CatalogDownloadResult result;
        result.packId = pack.id;
        result.errorMessage = tr("Failed to create catalog root directory: %1").arg(m_impl->catalogRootDirectory);
        emit downloadFinished(result);
        return;
    }

    const bool spccCatalog =
        pack.catalogFamilyId.trimmed().compare(QStringLiteral("spcc_gaia_dr3_xp_sampled"), Qt::CaseInsensitive) == 0;

    m_impl->activePack = pack;
    m_impl->activeResponseRepository = {};
    m_impl->activeSpccChunkRequest = SpccCatalogChunkDownloadRequest{
        .pack = pack,
        .installRootDirectory = m_impl->catalogRootDirectory,
        .mode = SpccCatalogChunkDownloadMode::FullCatalog,
    };
    m_impl->downloadMode = spccCatalog ? impl::DownloadMode::SpccCatalogChunks : impl::DownloadMode::Catalog;
    m_impl->useIncrementalCatalogInstall = spccCatalog;
    m_impl->manifest = {};
    m_impl->activeChunkTransfers.clear();
    m_impl->clearChunkVerifications();
    m_impl->activeChunk = {};
    m_impl->activeFinalPath.clear();
    m_impl->activePartPath.clear();
    m_impl->transferError.clear();
    m_impl->nextChunkIndex = 0;
    m_impl->verifiedBytes = 0;
    m_impl->verifiedChunks = 0;
    m_impl->lastCatalogProgressBytes = 0;
    m_impl->cancelled = false;

    const QString transferDirectory = m_impl->transferDirectory();
    if (!QDir().mkpath(transferDirectory))
    {
        CatalogDownloadResult result;
        result.packId = pack.id;
        result.errorMessage = tr("Failed to create catalog download directory: %1").arg(transferDirectory);
        emit downloadFinished(result);
        return;
    }

    startManifestDownload();
}

void CatalogDownloadService::downloadSpccCatalogChunks(const SpccCatalogChunkDownloadRequest& request)
{
    if (isBusy())
    {
        return;
    }

    CatalogPackDescriptor resolvedPack = request.pack;
    if (!isDownloadableManagedSpccCatalogDescriptor(resolvedPack))
    {
        std::optional<CatalogPackDescriptor> managedDescriptor =
            downloadableManagedSpccCatalogDescriptorForManifest(resolvedPack.id);
        if (!managedDescriptor.has_value() && resolvedPack.id.trimmed().isEmpty())
        {
            managedDescriptor = preferredDownloadableManagedSpccLocalFullCatalogDescriptor();
        }
        if (managedDescriptor.has_value())
        {
            LOG_INFO("SPCC catalog download descriptor rehydrated from managed pack registry: id='{}' url={}",
                     managedDescriptor->id.toStdString(), managedDescriptor->manifestUrl.toString().toStdString());
            resolvedPack = *managedDescriptor;
        }
    }

    CatalogDownloadResult earlyResult;
    earlyResult.packId = resolvedPack.id;
    if (!isUsableCatalogManifestUrl(resolvedPack.manifestUrl))
    {
        earlyResult.errorMessage =
            resolvedPack.id.trimmed().isEmpty()
                ? tr("SPCC catalog pack has no manifest URL configured.")
                : tr("SPCC catalog pack '%1' has no manifest URL configured.").arg(resolvedPack.id);
        emit downloadFinished(earlyResult);
        return;
    }
    if (!isSafePathSegment(resolvedPack.id))
    {
        earlyResult.errorMessage = tr("SPCC catalog pack has an unsafe ID.");
        emit downloadFinished(earlyResult);
        return;
    }
    if (resolvedPack.distributionType.trimmed().compare(QString::fromLatin1(kChunkDistributionType),
                                                        Qt::CaseInsensitive) != 0)
    {
        earlyResult.errorMessage =
            tr("Unsupported SPCC catalog distribution type: %1").arg(resolvedPack.distributionType);
        emit downloadFinished(earlyResult);
        return;
    }
    if (resolvedPack.catalogFamilyId.trimmed().compare(QStringLiteral("spcc_gaia_dr3_xp_sampled"),
                                                       Qt::CaseInsensitive) != 0)
    {
        earlyResult.errorMessage = tr("The selected catalog descriptor is not an SPCC Gaia XP sampled catalog.");
        emit downloadFinished(earlyResult);
        return;
    }

    m_impl->catalogRootDirectory = request.installRootDirectory.trimmed().isEmpty()
                                       ? m_impl->catalogRootDirectory
                                       : request.installRootDirectory.trimmed();
    if (!QDir().mkpath(m_impl->catalogRootDirectory))
    {
        earlyResult.errorMessage =
            tr("Failed to create SPCC catalog root directory: %1").arg(m_impl->catalogRootDirectory);
        emit downloadFinished(earlyResult);
        return;
    }

    m_impl->activePack = resolvedPack;
    m_impl->activeResponseRepository = {};
    m_impl->activeSpccChunkRequest = request;
    m_impl->activeSpccChunkRequest.pack = resolvedPack;
    m_impl->activeSpccChunkRequest.installRootDirectory = m_impl->catalogRootDirectory;
    m_impl->downloadMode = impl::DownloadMode::SpccCatalogChunks;
    m_impl->useIncrementalCatalogInstall = true;
    m_impl->manifest = {};
    m_impl->activeChunkTransfers.clear();
    m_impl->clearChunkVerifications();
    m_impl->activeChunk = {};
    m_impl->activeFinalPath.clear();
    m_impl->activePartPath.clear();
    m_impl->transferError.clear();
    m_impl->nextChunkIndex = 0;
    m_impl->verifiedBytes = 0;
    m_impl->verifiedChunks = 0;
    m_impl->lastCatalogProgressBytes = 0;
    m_impl->cancelled = false;

    const QString transferDirectory = m_impl->transferDirectory();
    if (!QDir().mkpath(transferDirectory))
    {
        earlyResult.errorMessage = tr("Failed to create SPCC catalog cache directory: %1").arg(transferDirectory);
        emit downloadFinished(earlyResult);
        return;
    }

    startManifestDownload();
}

void CatalogDownloadService::downloadSpccResponseRepository(const SpccResponseRepositoryDescriptor& repository)
{
    if (isBusy())
    {
        return;
    }

    CatalogDownloadResult earlyResult;
    earlyResult.packId = repository.id;
    if (!isUsableCatalogManifestUrl(repository.manifestUrl))
    {
        earlyResult.errorMessage = tr("SPCC response repository has no manifest URL configured.");
        emit downloadFinished(earlyResult);
        return;
    }
    if (!isSafePathSegment(repository.repositoryId) || !isSafePathSegment(repository.version))
    {
        earlyResult.errorMessage = tr("SPCC response repository identity is not safe for a managed install path.");
        emit downloadFinished(earlyResult);
        return;
    }
    if (!an::ui::isApprovedSpccResponseDescriptorRightsStatus(repository.rightsReviewStatus))
    {
        earlyResult.errorMessage = tr("SPCC response repository descriptor is not rights-approved.");
        emit downloadFinished(earlyResult);
        return;
    }
    if (!an::ui::isSupportedSpccResponseDeploymentScope(repository.deploymentScope))
    {
        earlyResult.errorMessage = tr("SPCC response repository descriptor has an unsupported deployment scope.");
        emit downloadFinished(earlyResult);
        return;
    }

    if (!QDir().mkpath(m_impl->spccResponseRepositoryRootDirectory))
    {
        earlyResult.errorMessage = tr("Failed to create SPCC response repository root directory: %1")
                                       .arg(m_impl->spccResponseRepositoryRootDirectory);
        emit downloadFinished(earlyResult);
        return;
    }

    m_impl->activePack = CatalogPackDescriptor{
        .id = repository.id,
        .displayName = repository.displayName,
        .description = repository.description,
        .manifestUrl = repository.manifestUrl,
        .distributionType = QStringLiteral("spcc-response-repository"),
        .downloadSizeBytes = repository.downloadSizeBytes,
        .installedSizeBytes = repository.installedSizeBytes,
    };
    m_impl->activeResponseRepository = repository;
    m_impl->activeSpccChunkRequest = {};
    m_impl->downloadMode = impl::DownloadMode::SpccResponseRepository;
    m_impl->useIncrementalCatalogInstall = false;
    m_impl->manifest = {};
    m_impl->activeChunkTransfers.clear();
    m_impl->clearChunkVerifications();
    m_impl->activeChunk = {};
    m_impl->activeFinalPath.clear();
    m_impl->activePartPath.clear();
    m_impl->transferError.clear();
    m_impl->nextChunkIndex = 0;
    m_impl->verifiedBytes = 0;
    m_impl->verifiedChunks = 0;
    m_impl->lastCatalogProgressBytes = 0;
    m_impl->cancelled = false;

    const QString stagingDirectory = m_impl->stagingDirectory();
    if (!QDir().mkpath(stagingDirectory))
    {
        earlyResult.errorMessage =
            tr("Failed to create SPCC response repository staging directory: %1").arg(stagingDirectory);
        emit downloadFinished(earlyResult);
        return;
    }

    (void)removePath(QDir(stagingDirectory).filePath(QString::fromLatin1(kInstallCompleteMarker)));
    startManifestDownload();
}

void CatalogDownloadService::cancel()
{
    if (!isBusy())
    {
        return;
    }

    m_impl->cancelled = true;
    if (m_impl->activeReply)
    {
        m_impl->activeReply->abort();
    }
    m_impl->abortActiveChunkTransfers();
    if (m_impl->activeFile.isOpen())
    {
        m_impl->activeFile.close();
    }
}

void CatalogDownloadService::emitProgress(const QString& phase, const qint64 bytesReceived, const qint64 bytesTotal,
                                          const int filesCompleted, const int filesTotal, const QString& currentFile)
{
    qint64 reportedBytesReceived = bytesReceived;
    if (phase != QStringLiteral("manifest") && bytesTotal > 0)
    {
        reportedBytesReceived = (std::min)((std::max)(qint64{0}, reportedBytesReceived), bytesTotal);
        reportedBytesReceived = (std::max)(reportedBytesReceived, m_impl->lastCatalogProgressBytes);
        m_impl->lastCatalogProgressBytes = reportedBytesReceived;
    }

    CatalogDownloadProgress progress;
    progress.packId = m_impl->activePack.id;
    progress.phase = phase;
    progress.bytesReceived = reportedBytesReceived;
    progress.bytesTotal = bytesTotal;
    progress.filesCompleted = filesCompleted;
    progress.filesTotal = filesTotal;
    progress.currentFile = currentFile;
    emit downloadProgress(progress);
}

void CatalogDownloadService::startManifestDownload()
{
    m_impl->phase = impl::Phase::DownloadingManifest;
    m_impl->transferKind = impl::TransferKind::Manifest;
    m_impl->transferError.clear();
    m_impl->activeChunk = {};
    m_impl->activeFinalPath = m_impl->manifestPath();
    m_impl->activePartPath = partFilePath(m_impl->activeFinalPath);

    (void)removePath(m_impl->activePartPath);

    if (!ensureParentDirectory(m_impl->activePartPath))
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage = tr("Failed to prepare catalog manifest destination: %1").arg(m_impl->activePartPath);
        finishWithResult(std::move(result));
        return;
    }

    m_impl->activeFile.setFileName(m_impl->activePartPath);
    if (!m_impl->activeFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage = tr("Failed to open manifest destination: %1").arg(m_impl->activePartPath);
        finishWithResult(std::move(result));
        return;
    }

    LOG_INFO("Catalog manifest download: pack='{}' url={}", m_impl->activePack.id.toStdString(),
             m_impl->activePack.manifestUrl.toString().toStdString());

    QNetworkRequest request(m_impl->activePack.manifestUrl);
    applyCatalogDownloadRequestHeaders(request);
    m_impl->activeReply = m_impl->networkManager.get(request);
    connect(m_impl->activeReply.data(), &QNetworkReply::readyRead, this, &CatalogDownloadService::handleReplyReadyRead);
    connect(m_impl->activeReply.data(), &QNetworkReply::downloadProgress, this,
            &CatalogDownloadService::handleReplyDownloadProgress);
    connect(m_impl->activeReply.data(), &QNetworkReply::finished, this, &CatalogDownloadService::handleReplyFinished);

    emitProgress(QStringLiteral("manifest"), 0, 0, 0, 0, m_impl->manifestFileName());
}

bool CatalogDownloadService::startChunkDownload(const int chunkIndex)
{
    if (chunkIndex < 0 || chunkIndex >= m_impl->manifest.files.size())
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage = tr("Catalog download chunk index is out of range.");
        finishWithResult(std::move(result));
        return false;
    }

    m_impl->phase = impl::Phase::DownloadingChunk;
    m_impl->transferKind = impl::TransferKind::Chunk;
    auto transfer = std::make_unique<ActiveChunkTransfer>();
    transfer->chunkIndex = chunkIndex;
    transfer->chunk = m_impl->manifest.files.at(chunkIndex);
    transfer->finalPath = QDir(m_impl->transferDirectory()).filePath(transfer->chunk.file);
    transfer->partPath = partFilePath(transfer->finalPath);

    (void)removePath(transfer->partPath);

    if (!ensureParentDirectory(transfer->partPath))
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage = tr("Failed to prepare catalog chunk destination: %1").arg(transfer->partPath);
        finishWithResult(std::move(result));
        return false;
    }

    transfer->file.setFileName(transfer->partPath);
    if (!transfer->file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage = tr("Failed to open chunk destination: %1").arg(transfer->partPath);
        finishWithResult(std::move(result));
        return false;
    }

    LOG_INFO("Catalog chunk download: pack='{}' chunk='{}' url={}", m_impl->activePack.id.toStdString(),
             transfer->chunk.file.toStdString(), transfer->chunk.url.toString().toStdString());

    QNetworkRequest request(transfer->chunk.url);
    applyCatalogDownloadRequestHeaders(request);
    QNetworkReply* reply = m_impl->networkManager.get(request);
    transfer->reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, &CatalogDownloadService::handleReplyReadyRead);
    connect(reply, &QNetworkReply::downloadProgress, this, &CatalogDownloadService::handleReplyDownloadProgress);
    connect(reply, &QNetworkReply::finished, this, &CatalogDownloadService::handleReplyFinished);

    const QString currentFile = transfer->chunk.file;
    m_impl->activeChunkTransfers.emplace(reply, std::move(transfer));
    emitProgress(QStringLiteral("download"), m_impl->verifiedBytes + m_impl->activeChunkBytesReceived(),
                 m_impl->manifest.totalBytes, m_impl->verifiedChunks, m_impl->manifest.files.size(), currentFile);
    return true;
}

void CatalogDownloadService::handleReplyReadyRead()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply)
    {
        const auto transferIt = m_impl->activeChunkTransfers.find(reply);
        if (transferIt != m_impl->activeChunkTransfers.end())
        {
            ActiveChunkTransfer& transfer = *transferIt->second;
            if (!transfer.file.isOpen())
            {
                return;
            }

            const QByteArray chunk = reply->readAll();
            if (chunk.isEmpty())
            {
                return;
            }

            const qint64 written = transfer.file.write(chunk);
            if (written != chunk.size())
            {
                transfer.error = tr("Failed to write downloaded catalog data to %1").arg(transfer.file.fileName());
                reply->abort();
            }
            return;
        }
    }

    if (!m_impl->activeReply || !m_impl->activeFile.isOpen())
    {
        return;
    }

    const QByteArray chunk = m_impl->activeReply->readAll();
    if (chunk.isEmpty())
    {
        return;
    }

    const qint64 written = m_impl->activeFile.write(chunk);
    if (written != chunk.size())
    {
        m_impl->transferError = tr("Failed to write downloaded catalog data to %1").arg(m_impl->activeFile.fileName());
        m_impl->activeReply->abort();
    }
}

void CatalogDownloadService::handleReplyDownloadProgress(const qint64 bytesReceived, const qint64 bytesTotal)
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply)
    {
        const auto transferIt = m_impl->activeChunkTransfers.find(reply);
        if (transferIt != m_impl->activeChunkTransfers.end())
        {
            ActiveChunkTransfer& transfer = *transferIt->second;
            transfer.bytesReceived = bytesReceived;
            (void)bytesTotal;
            emitProgress(QStringLiteral("download"), m_impl->verifiedBytes + m_impl->activeChunkBytesReceived(),
                         m_impl->manifest.totalBytes, m_impl->verifiedChunks, m_impl->manifest.files.size(),
                         transfer.chunk.file);
            return;
        }
    }

    if (m_impl->transferKind == impl::TransferKind::Manifest)
    {
        emitProgress(QStringLiteral("manifest"), bytesReceived, bytesTotal, 0, 0, m_impl->manifestFileName());
        return;
    }
}

void CatalogDownloadService::finishWithResult(CatalogDownloadResult result)
{
    const bool removeCancelledStaging = result.cancelled && !m_impl->useIncrementalCatalogInstall;
    const QString cancelledStagingDirectory = removeCancelledStaging ? m_impl->stagingDirectory() : QString();
    const QString activePartPath = m_impl->activePartPath;

    if (m_impl->activeFile.isOpen())
    {
        m_impl->activeFile.close();
    }

    m_impl->phase = impl::Phase::Idle;
    m_impl->transferKind = impl::TransferKind::None;
    ++m_impl->spccSelectionTicket;
    m_impl->activeReply.clear();
    m_impl->clearActiveChunkTransfers();
    m_impl->clearChunkVerifications();
    if (!activePartPath.isEmpty())
    {
        (void)removePath(activePartPath);
    }
    if (removeCancelledStaging)
    {
        (void)removePath(cancelledStagingDirectory);
    }
    m_impl->activeChunk = {};
    m_impl->activeFinalPath.clear();
    m_impl->activePartPath.clear();
    m_impl->transferError.clear();
    m_impl->activeResponseRepository = {};
    m_impl->activeSpccChunkRequest = {};
    m_impl->downloadMode = impl::DownloadMode::Catalog;
    m_impl->useIncrementalCatalogInstall = false;
    m_impl->lastCatalogProgressBytes = 0;

    emit downloadFinished(result);
}

void CatalogDownloadService::handleManifestDownloaded()
{
    QFile manifestFile(m_impl->manifestPath());
    if (!manifestFile.open(QIODevice::ReadOnly))
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage = tr("Failed to open downloaded catalog manifest: %1").arg(m_impl->manifestPath());
        finishWithResult(std::move(result));
        return;
    }

    QString errorMessage;
    const QByteArray manifestBytes = manifestFile.readAll();
    std::optional<ParsedManifest> parsedManifest;
    if (m_impl->downloadMode == impl::DownloadMode::SpccResponseRepository)
    {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(manifestBytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            CatalogDownloadResult result;
            result.packId = m_impl->activePack.id;
            result.errorMessage =
                tr("Failed to parse SPCC response repository manifest: %1").arg(parseError.errorString());
            finishWithResult(std::move(result));
            return;
        }
        parsedManifest = parseSpccResponseRepositoryMeta(manifestBytes, doc.object(), m_impl->activePack.manifestUrl,
                                                         m_impl->activeResponseRepository, &errorMessage);
    }
    else
    {
        parsedManifest =
            parseManifestBytes(manifestBytes, m_impl->activePack.manifestUrl, m_impl->activePack, &errorMessage);
    }
    manifestFile.close();
    if (!parsedManifest.has_value())
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage = errorMessage;
        finishWithResult(std::move(result));
        return;
    }

    m_impl->manifest = *parsedManifest;
    if (m_impl->downloadMode == impl::DownloadMode::SpccCatalogChunks &&
        needsSpccChunkSelection(m_impl->manifest, m_impl->activeSpccChunkRequest))
    {
        selectSpccChunksThenDownload();
        return;
    }
    startChunkDownloads();
}

void CatalogDownloadService::selectSpccChunksThenDownload()
{
    const an::ui::IEngineSessionProvider* provider = process_utils::currentEngineSessionProvider();
    if (provider == nullptr)
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage = tr("No engine session is available to plan the SPCC catalog chunks.");
        finishWithResult(std::move(result));
        return;
    }

    // The engine selects the chunks on a worker; the download goes on here once it lands, if it is still this one.
    const an::consumer::EngineSession* session = &provider->engineSession();
    const std::uint64_t ticket = ++m_impl->spccSelectionTicket;
    QPointer<CatalogDownloadService> guard(this);
    QThreadPool::globalInstance()->start([guard, ticket, session,
                                          manifest = spccManifestFromParsedManifest(m_impl->manifest),
                                          request = m_impl->activeSpccChunkRequest]() {
        an::Result<QSet<int>> selected = [&]() -> an::Result<QSet<int>> {
            an::Result<an::consumer::OperationScope> scope =
                session->admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
            if (!scope.hasValue())
            {
                return scope.status();
            }
            return selectRequiredSpccChunks(scope->execution(), manifest, request);
        }();
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [guard, ticket, selected = std::move(selected)]() {
                if (!guard || guard->m_impl->spccSelectionTicket != ticket)
                {
                    return;
                }
                guard->continueAfterSpccChunkSelection(selected);
            },
            Qt::QueuedConnection);
    });
}

void CatalogDownloadService::continueAfterSpccChunkSelection(const an::Result<QSet<int>>& selected)
{
    if (m_impl->cancelled)
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.cancelled = true;
        result.errorMessage = tr("Catalog download cancelled.");
        finishWithResult(std::move(result));
        return;
    }
    if (!selected.hasValue())
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage = QString::fromUtf8(selected.status().message());
        finishWithResult(std::move(result));
        return;
    }
    keepRequiredSpccChunks(m_impl->manifest, *selected);
    startChunkDownloads();
}

void CatalogDownloadService::startChunkDownloads()
{
    m_impl->nextChunkIndex = 0;
    m_impl->verifiedBytes = 0;
    m_impl->verifiedChunks = 0;

    processNextChunk();
}

void CatalogDownloadService::handleChunkDownloaded(QNetworkReply* reply)
{
    auto transferIt = m_impl->activeChunkTransfers.find(reply);
    if (transferIt == m_impl->activeChunkTransfers.end())
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage = tr("Unexpected catalog downloader state.");
        finishWithResult(std::move(result));
        return;
    }

    std::unique_ptr<ActiveChunkTransfer> transfer = std::move(transferIt->second);
    m_impl->activeChunkTransfers.erase(transferIt);

    m_impl->pendingChunkVerifications.push_back(std::move(transfer));
    processPendingChunkVerifications();
    processNextChunk();
}

void CatalogDownloadService::processPendingChunkVerifications()
{
    if (m_impl->phase == impl::Phase::Idle || m_impl->verifyingChunk || m_impl->pendingChunkVerifications.empty())
    {
        return;
    }

    if (m_impl->cancelled)
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.cancelled = true;
        result.errorMessage = tr("Catalog download cancelled.");
        finishWithResult(std::move(result));
        return;
    }

    m_impl->verifyingChunk = std::move(m_impl->pendingChunkVerifications.front());
    m_impl->pendingChunkVerifications.pop_front();
    m_impl->verifyingBytesRead = 0;
    m_impl->verifyingHash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_impl->phase = impl::Phase::VerifyingChunk;
    emitProgress(QStringLiteral("verify"), m_impl->verifiedBytes, m_impl->manifest.totalBytes, m_impl->verifiedChunks,
                 m_impl->manifest.files.size(), m_impl->verifyingChunk->chunk.file);

    const QFileInfo partInfo(m_impl->verifyingChunk->partPath);
    if (!partInfo.exists() ||
        (m_impl->verifyingChunk->chunk.requireSizeMatch && partInfo.size() != m_impl->verifyingChunk->chunk.sizeBytes))
    {
        (void)removePath(m_impl->verifyingChunk->partPath);
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage =
            tr("Downloaded catalog chunk has the wrong size: %1").arg(m_impl->verifyingChunk->chunk.file);
        finishWithResult(std::move(result));
        return;
    }

    m_impl->verifyingFile.setFileName(m_impl->verifyingChunk->partPath);
    if (!m_impl->verifyingFile.open(QIODevice::ReadOnly))
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage =
            tr("Failed to open downloaded catalog chunk for verification: %1").arg(m_impl->verifyingChunk->chunk.file);
        finishWithResult(std::move(result));
        return;
    }

    QTimer::singleShot(0, this, &CatalogDownloadService::continueChunkVerification);
}

void CatalogDownloadService::continueChunkVerification()
{
    if (m_impl->phase == impl::Phase::Idle || !m_impl->verifyingChunk || !m_impl->verifyingHash)
    {
        return;
    }

    if (m_impl->cancelled)
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.cancelled = true;
        result.errorMessage = tr("Catalog download cancelled.");
        finishWithResult(std::move(result));
        return;
    }

    const QByteArray payload = m_impl->verifyingFile.read(kVerificationReadChunkBytes);
    if (!payload.isEmpty())
    {
        m_impl->verifyingHash->addData(payload);
        m_impl->verifyingBytesRead += payload.size();
        emitProgress(QStringLiteral("verify"),
                     m_impl->verifiedBytes + m_impl->activeChunkBytesReceived() + m_impl->verifyingBytesRead,
                     m_impl->manifest.totalBytes, m_impl->verifiedChunks, m_impl->manifest.files.size(),
                     m_impl->verifyingChunk->chunk.file);
        QTimer::singleShot(0, this, &CatalogDownloadService::continueChunkVerification);
        return;
    }

    if (m_impl->verifyingFile.error() != QFileDevice::NoError)
    {
        if (m_impl->verifyingFile.isOpen())
        {
            m_impl->verifyingFile.close();
        }
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage =
            tr("Failed to read downloaded catalog chunk for verification: %1").arg(m_impl->verifyingChunk->chunk.file);
        finishWithResult(std::move(result));
        return;
    }

    const QByteArray actualHash = m_impl->verifyingHash->result().toHex().toLower();
    if (actualHash != m_impl->verifyingChunk->chunk.sha256Hex)
    {
        if (m_impl->verifyingFile.isOpen())
        {
            m_impl->verifyingFile.close();
        }
        (void)removePath(m_impl->verifyingChunk->partPath);
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage =
            tr("Checksum verification failed for catalog chunk: %1").arg(m_impl->verifyingChunk->chunk.file);
        finishWithResult(std::move(result));
        return;
    }

    if (m_impl->verifyingFile.isOpen())
    {
        m_impl->verifyingFile.close();
    }

    (void)removePath(m_impl->verifyingChunk->finalPath);
    if (!QFile::rename(m_impl->verifyingChunk->partPath, m_impl->verifyingChunk->finalPath))
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage =
            tr("Failed to finalize downloaded catalog chunk: %1").arg(m_impl->verifyingChunk->chunk.file);
        finishWithResult(std::move(result));
        return;
    }

    const QString verifiedFile = m_impl->verifyingChunk->chunk.file;
    m_impl->verifiedBytes += m_impl->verifyingChunk->chunk.sizeBytes;
    m_impl->verifiedChunks += 1;
    m_impl->verifyingBytesRead = 0;
    m_impl->verifyingHash.reset();
    m_impl->verifyingChunk.reset();

    emitProgress(QStringLiteral("verify"), m_impl->verifiedBytes, m_impl->manifest.totalBytes, m_impl->verifiedChunks,
                 m_impl->manifest.files.size(), verifiedFile);

    processPendingChunkVerifications();
    processNextChunk();
}

void CatalogDownloadService::processNextChunk()
{
    if (m_impl->cancelled)
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.cancelled = true;
        result.errorMessage = tr("Catalog download cancelled.");
        finishWithResult(std::move(result));
        return;
    }

    while (m_impl->activeChunkTransfers.size() < static_cast<std::size_t>(kMaxConcurrentChunkDownloads) &&
           m_impl->nextChunkIndex < m_impl->manifest.files.size())
    {
        const int chunkIndex = m_impl->nextChunkIndex;
        const ManifestChunk& chunk = m_impl->manifest.files.at(chunkIndex);
        const QString chunkPath = QDir(m_impl->transferDirectory()).filePath(chunk.file);

        m_impl->phase = impl::Phase::VerifyingChunk;
        emitProgress(QStringLiteral("verify"), m_impl->verifiedBytes + m_impl->activeChunkBytesReceived(),
                     m_impl->manifest.totalBytes, m_impl->verifiedChunks, m_impl->manifest.files.size(), chunk.file);

        QString verificationError;
        switch (inspectLocalChunk(chunk, chunkPath, &verificationError))
        {
        case LocalChunkState::Verified:
            m_impl->verifiedBytes += chunk.sizeBytes;
            m_impl->verifiedChunks += 1;
            m_impl->nextChunkIndex += 1;
            emitProgress(QStringLiteral("verify"), m_impl->verifiedBytes + m_impl->activeChunkBytesReceived(),
                         m_impl->manifest.totalBytes, m_impl->verifiedChunks, m_impl->manifest.files.size(),
                         chunk.file);
            continue;

        case LocalChunkState::NeedsRedownload:
            (void)removePath(chunkPath);
            [[fallthrough]];

        case LocalChunkState::Missing:
            if (!startChunkDownload(chunkIndex))
            {
                return;
            }
            m_impl->nextChunkIndex += 1;
            continue;

        case LocalChunkState::Error:
        default: {
            CatalogDownloadResult result;
            result.packId = m_impl->activePack.id;
            result.errorMessage = verificationError;
            finishWithResult(std::move(result));
            return;
        }
        }
    }

    if (!m_impl->verifyingChunk && !m_impl->pendingChunkVerifications.empty())
    {
        processPendingChunkVerifications();
    }

    if (m_impl->nextChunkIndex >= m_impl->manifest.files.size() && m_impl->activeChunkTransfers.empty() &&
        m_impl->pendingChunkVerifications.empty() && !m_impl->verifyingChunk)
    {
        QString errorMessage;
        if (m_impl->useIncrementalCatalogInstall)
        {
            if (m_impl->activeSpccChunkRequest.mode == SpccCatalogChunkDownloadMode::FullCatalog &&
                !writeInstallMarker(m_impl->installDirectory(), &errorMessage))
            {
                CatalogDownloadResult result;
                result.packId = m_impl->activePack.id;
                result.errorMessage = errorMessage;
                finishWithResult(std::move(result));
                return;
            }

            CatalogDownloadResult result;
            result.success = true;
            result.packId = m_impl->activePack.id;
            result.installDirectory = m_impl->installDirectory();
            finishWithResult(std::move(result));
            return;
        }

        if (!writeInstallMarker(m_impl->stagingDirectory(), &errorMessage))
        {
            CatalogDownloadResult result;
            result.packId = m_impl->activePack.id;
            result.errorMessage = errorMessage;
            finishWithResult(std::move(result));
            return;
        }

        if (!promoteStagingDirectory(m_impl->stagingDirectory(), m_impl->installDirectory(), m_impl->backupDirectory(),
                                     &errorMessage))
        {
            CatalogDownloadResult result;
            result.packId = m_impl->activePack.id;
            result.errorMessage = errorMessage;
            finishWithResult(std::move(result));
            return;
        }

        CatalogDownloadResult result;
        result.success = true;
        result.packId = m_impl->activePack.id;
        result.installDirectory = m_impl->installDirectory();
        finishWithResult(std::move(result));
        return;
    }
}

void CatalogDownloadService::handleReplyFinished()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply == nullptr)
    {
        return;
    }

    const bool isChunkReply = m_impl->activeChunkTransfers.find(reply) != m_impl->activeChunkTransfers.end();
    if (!isChunkReply && m_impl->activeReply != reply)
    {
        reply->deleteLater();
        return;
    }

    const QNetworkReply::NetworkError errorCode = reply->error();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!isChunkReply)
    {
        m_impl->activeReply.clear();
    }
    const auto discardChunkTransfer = [this, isChunkReply, reply] {
        if (isChunkReply)
        {
            m_impl->activeChunkTransfers.erase(reply);
        }
    };

    QString transferError;
    if (isChunkReply)
    {
        const auto transferIt = m_impl->activeChunkTransfers.find(reply);
        if (transferIt != m_impl->activeChunkTransfers.end() && transferIt->second)
        {
            ActiveChunkTransfer& transfer = *transferIt->second;
            if (transfer.file.isOpen())
            {
                const QByteArray payload = reply->readAll();
                if (!payload.isEmpty())
                {
                    const qint64 written = transfer.file.write(payload);
                    if (written != payload.size() && transfer.error.isEmpty())
                    {
                        transfer.error =
                            tr("Failed to write downloaded catalog data to %1").arg(transfer.file.fileName());
                    }
                }
                transfer.file.close();
            }
            transferError = transfer.error;
        }
    }
    else if (m_impl->activeFile.isOpen())
    {
        const QByteArray payload = reply->readAll();
        if (!payload.isEmpty())
        {
            const qint64 written = m_impl->activeFile.write(payload);
            if (written != payload.size() && m_impl->transferError.isEmpty())
            {
                m_impl->transferError =
                    tr("Failed to write downloaded catalog data to %1").arg(m_impl->activeFile.fileName());
            }
        }
        m_impl->activeFile.close();
    }

    if (m_impl->cancelled)
    {
        discardChunkTransfer();
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.cancelled = true;
        result.errorMessage = tr("Catalog download cancelled.");
        finishWithResult(std::move(result));
        reply->deleteLater();
        return;
    }

    if (isChunkReply && !transferError.isEmpty())
    {
        discardChunkTransfer();
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage = transferError;
        finishWithResult(std::move(result));
        reply->deleteLater();
        return;
    }

    if (!isChunkReply && !m_impl->transferError.isEmpty())
    {
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        result.errorMessage = m_impl->transferError;
        finishWithResult(std::move(result));
        reply->deleteLater();
        return;
    }

    if (errorCode != QNetworkReply::NoError)
    {
        discardChunkTransfer();
        CatalogDownloadResult result;
        result.packId = m_impl->activePack.id;
        if ((httpStatus == 401 || httpStatus == 403) && isHuggingFaceUrl(reply->url()))
        {
            result.errorMessage = tr("Catalog download failed: Hugging Face requires authentication for this "
                                     "dataset. Make the dataset public or restart AfterNight with HF_TOKEN or "
                                     "HUGGINGFACE_HUB_TOKEN set.");
        }
        else
        {
            result.errorMessage = tr("Catalog download failed: %1").arg(reply->errorString());
        }

        LOG_WARNING("Catalog transfer failed: pack='{}' status={} error={}", m_impl->activePack.id.toStdString(),
                    httpStatus, reply->errorString().toStdString());

        finishWithResult(std::move(result));
        reply->deleteLater();
        return;
    }

    if (isChunkReply)
    {
        handleChunkDownloaded(reply);
        reply->deleteLater();
        return;
    }

    if (m_impl->transferKind == impl::TransferKind::Manifest)
    {
        (void)removePath(m_impl->activeFinalPath);
        if (!QFile::rename(m_impl->activePartPath, m_impl->activeFinalPath))
        {
            CatalogDownloadResult result;
            result.packId = m_impl->activePack.id;
            result.errorMessage = tr("Failed to finalize downloaded catalog manifest.");
            finishWithResult(std::move(result));
            reply->deleteLater();
            return;
        }

        handleManifestDownloaded();
        reply->deleteLater();
        return;
    }

    CatalogDownloadResult result;
    result.packId = m_impl->activePack.id;
    result.errorMessage = tr("Unexpected catalog downloader state.");
    finishWithResult(std::move(result));
    reply->deleteLater();
}

} // namespace an::ui
