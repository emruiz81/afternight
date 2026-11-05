// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "app/CatalogDownloadService.h"
#include "app/PlateSolveTypes.h"
#include "controls/IAppContext.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "app/ApplicationStorage.h"

namespace an::ui
{

enum class GaiaCatalogDirectoryStatus
{
    Missing,
    Ready,
    UnsupportedFormat,
    IncompleteInstall,
    Invalid,
};

struct GaiaCatalogDirectoryInspection
{
    GaiaCatalogDirectoryStatus status = GaiaCatalogDirectoryStatus::Missing;
    QString directoryPath;
    QString catalogId;
    QString catalogName;
    bool hasInstallMarker = false;
    int chunkCount = 0;
    int chunkFilesPresent = 0;
};

struct GaiaCatalogPackLocation
{
    QString packId;
    QString directoryPath;
    QString displayName;
    GaiaCatalogDirectoryInspection inspection;
};

[[nodiscard]] inline GaiaCatalogDirectoryInspection inspectGaiaCatalogDirectoryDetailed(const QString& directoryPath)
{
    static const QString kInstallCompleteMarker = QStringLiteral(".install_complete");
    static const QString kCatalogMetaFileName = QStringLiteral("catalog_meta.json");
    static const QString kChunkStorage = QStringLiteral("chunks");

    GaiaCatalogDirectoryInspection inspection;
    inspection.directoryPath = directoryPath.trimmed();

    const QString normalizedPath = inspection.directoryPath;
    if (normalizedPath.isEmpty())
    {
        return inspection;
    }

    const QFileInfo directoryInfo(normalizedPath);
    if (!directoryInfo.exists())
    {
        return inspection;
    }
    if (!directoryInfo.isDir())
    {
        inspection.status = GaiaCatalogDirectoryStatus::Invalid;
        return inspection;
    }

    const QDir rootDir(normalizedPath);
    inspection.hasInstallMarker = QFileInfo::exists(rootDir.filePath(kInstallCompleteMarker));
    const QString metaPath = rootDir.filePath(kCatalogMetaFileName);
    QFile metaFile(metaPath);
    if (metaFile.open(QIODevice::ReadOnly))
    {
        const QJsonDocument metaDoc = QJsonDocument::fromJson(metaFile.readAll());
        metaFile.close();
        if (!metaDoc.isObject())
        {
            inspection.status = GaiaCatalogDirectoryStatus::Invalid;
            return inspection;
        }

        const QJsonObject metaObject = metaDoc.object();
        inspection.catalogId = metaObject.value(QStringLiteral("catalog_id")).toString().trimmed();
        inspection.catalogName = metaObject.value(QStringLiteral("catalog_name")).toString().trimmed();

        const QString storage = metaObject.value(QStringLiteral("storage")).toString().trimmed();
        if (storage == kChunkStorage)
        {
            const int formatVersion = metaObject.value(QStringLiteral("format_version")).toInt();
            if (formatVersion != 3)
            {
                inspection.status = GaiaCatalogDirectoryStatus::Invalid;
                return inspection;
            }

            const QJsonValue chunksValue = metaObject.value(QStringLiteral("chunks"));
            if (!chunksValue.isArray())
            {
                inspection.status = GaiaCatalogDirectoryStatus::Invalid;
                return inspection;
            }

            const QJsonArray chunksArray = chunksValue.toArray();
            inspection.chunkCount = metaObject.value(QStringLiteral("chunk_count")).toInt(chunksArray.size());
            if (inspection.chunkCount <= 0 || inspection.chunkCount != chunksArray.size())
            {
                inspection.status = GaiaCatalogDirectoryStatus::Invalid;
                return inspection;
            }

            for (const QJsonValue& chunkValue : chunksArray)
            {
                if (!chunkValue.isObject())
                {
                    inspection.status = GaiaCatalogDirectoryStatus::Invalid;
                    return inspection;
                }

                const QJsonObject chunkObject = chunkValue.toObject();
                const QString chunkFile = chunkObject.value(QStringLiteral("file")).toString().trimmed();
                if (chunkFile.isEmpty())
                {
                    inspection.status = GaiaCatalogDirectoryStatus::Invalid;
                    return inspection;
                }

                const QFileInfo chunkInfo(rootDir.filePath(chunkFile));
                if (!chunkInfo.exists() || !chunkInfo.isFile())
                {
                    inspection.status = GaiaCatalogDirectoryStatus::IncompleteInstall;
                    return inspection;
                }

                const qint64 expectedSize = chunkObject.value(QStringLiteral("size_bytes")).toVariant().toLongLong();
                if (expectedSize > 0 && chunkInfo.size() != expectedSize)
                {
                    inspection.status = GaiaCatalogDirectoryStatus::IncompleteInstall;
                    return inspection;
                }

                ++inspection.chunkFilesPresent;
            }

            inspection.status = GaiaCatalogDirectoryStatus::Ready;
            return inspection;
        }

        inspection.status = GaiaCatalogDirectoryStatus::UnsupportedFormat;
        return inspection;
    }

    const QFileInfoList tileDirectories =
        rootDir.entryInfoList({QStringLiteral("healpix_order*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    if (!tileDirectories.isEmpty())
    {
        inspection.status = GaiaCatalogDirectoryStatus::UnsupportedFormat;
        return inspection;
    }

    const QFileInfoList entries = rootDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    inspection.status = entries.isEmpty() ? GaiaCatalogDirectoryStatus::Missing : GaiaCatalogDirectoryStatus::Invalid;
    return inspection;
}

[[nodiscard]] inline GaiaCatalogDirectoryStatus inspectGaiaCatalogDirectory(const QString& directoryPath)
{
    return inspectGaiaCatalogDirectoryDetailed(directoryPath).status;
}

[[nodiscard]] inline bool isGaiaCatalogReady(const QString& directoryPath)
{
    return inspectGaiaCatalogDirectory(directoryPath) == GaiaCatalogDirectoryStatus::Ready;
}

[[nodiscard]] inline GaiaCatalogDirectoryInspection inspectGaiaCatalogPackInstall(const QString& catalogRootDirectory,
                                                                                  const QString& packId)
{
    const QString normalizedRoot = catalogRootDirectory.trimmed();
    const QString normalizedPack = packId.trimmed();
    if (normalizedRoot.isEmpty() || normalizedPack.isEmpty())
    {
        return {};
    }

    return inspectGaiaCatalogDirectoryDetailed(QDir(normalizedRoot).filePath(normalizedPack));
}

[[nodiscard]] inline bool isTransientGaiaCatalogDirectoryName(const QString& directoryName)
{
    return directoryName.endsWith(QStringLiteral(".installing"), Qt::CaseInsensitive) ||
           directoryName.endsWith(QStringLiteral(".backup"), Qt::CaseInsensitive);
}

[[nodiscard]] inline QVector<GaiaCatalogPackLocation> scanGaiaCatalogPacks(const QString& directoryPath)
{
    QVector<GaiaCatalogPackLocation> packs;
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
                               const GaiaCatalogDirectoryInspection& inspection) {
        const QString packId = !inspection.catalogId.isEmpty() ? inspection.catalogId : fallbackId.trimmed();
        if (packId.isEmpty())
        {
            return;
        }

        packs.push_back(GaiaCatalogPackLocation{
            .packId = packId,
            .directoryPath = path,
            .displayName = !inspection.catalogName.isEmpty() ? inspection.catalogName : packId,
            .inspection = inspection,
        });
    };

    const GaiaCatalogDirectoryInspection rootInspection = inspectGaiaCatalogDirectoryDetailed(normalizedPath);
    if (isTransientGaiaCatalogDirectoryName(rootInfo.fileName()))
    {
        return packs;
    }
    if (!rootInspection.catalogId.isEmpty() || rootInspection.status == GaiaCatalogDirectoryStatus::Ready ||
        rootInspection.status == GaiaCatalogDirectoryStatus::IncompleteInstall)
    {
        appendPack(normalizedPath, rootInfo.fileName(), rootInspection);
        return packs;
    }

    const QDir rootDir(normalizedPath);
    const QFileInfoList childDirs = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& childDir : childDirs)
    {
        if (isTransientGaiaCatalogDirectoryName(childDir.fileName()))
        {
            continue;
        }

        const GaiaCatalogDirectoryInspection inspection =
            inspectGaiaCatalogDirectoryDetailed(childDir.absoluteFilePath());
        if (inspection.catalogId.isEmpty() && inspection.status != GaiaCatalogDirectoryStatus::Ready &&
            inspection.status != GaiaCatalogDirectoryStatus::IncompleteInstall)
        {
            continue;
        }

        appendPack(childDir.absoluteFilePath(), childDir.fileName(), inspection);
    }

    return packs;
}

[[nodiscard]] inline QString defaultGaiaCatalogPack()
{
    return QString(SettingsDefaults::PLATESOLVE_GAIA_CATALOG_PACK);
}

[[nodiscard]] inline QString defaultGaiaCatalogDirectory()
{
    return CatalogDownloadService::defaultCatalogRootDirectory();
}

/// Reads the primary catalog mirror base URL from catalog_mirrors.json stored
/// in the application data directory.
/// If the file does not exist it is created with the built-in default mirrors.
/// If the file exists it is left untouched — users can edit it to override the
/// mirror URL without recompiling.
[[nodiscard]] inline QString loadCatalogMirrorBaseUrl()
{
    static const QString kDefaultBaseUrl =
        QStringLiteral("https://huggingface.co/datasets/emruiz81/afternight-gaia-catalogs/resolve/main");
    static const int kCurrentVersion = 1;
    static const QString kFileName = QStringLiteral("catalog_mirrors.json");

    const QString dataDir = applicationLocalDataPath();
    const QString filePath = QDir(dataDir).filePath(kFileName);

    // Create with defaults if absent
    if (!QFileInfo::exists(filePath))
    {
        QDir().mkpath(dataDir);
        QJsonObject root;
        root[QStringLiteral("version")] = kCurrentVersion;
        root[QStringLiteral("mirrors")] = QJsonArray{kDefaultBaseUrl};
        QFile out(filePath);
        if (out.open(QIODevice::WriteOnly))
        {
            out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            out.close();
        }
    }

    // Read
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly))
    {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (doc.isObject())
        {
            const QJsonArray mirrors = doc.object().value(QStringLiteral("mirrors")).toArray();
            if (!mirrors.isEmpty())
            {
                const QString url = mirrors.first().toString().trimmed();
                if (!url.isEmpty())
                    return url.endsWith(QLatin1Char('/')) ? url.chopped(1) : url;
            }
        }
    }

    return kDefaultBaseUrl;
}

[[nodiscard]] inline QVector<CatalogPackDescriptor> availableGaiaCatalogPacks()
{
    // Each manifest references sibling .ansp chunk files in the same dataset directory.
    // Chunk URLs are resolved relative to the manifest URL via QUrl::resolved().
    // The base URL is loaded from catalog_mirrors.json (see loadCatalogMirrorBaseUrl()).
    const QString base_url = loadCatalogMirrorBaseUrl();

    return {
        CatalogPackDescriptor{
            .id = QStringLiteral("gaia_dr3_g12"),
            .displayName = QStringLiteral("Gaia DR3 G<=12"),
            .description = QStringLiteral("Compact all-sky catalog (~3.1M stars, G<=12). "
                                          "Best for fast setup, wide fields, and bright-target work."),
            .manifestUrl = QUrl(base_url + QStringLiteral("/gaia_dr3_g12/catalog_meta.json")),
            .distributionType = QStringLiteral("chunks"),
            .recommended = false,
            .downloadSizeBytes = 188LL * 1024LL * 1024LL,
            .installedSizeBytes = 188LL * 1024LL * 1024LL,
        },
        CatalogPackDescriptor{
            .id = QStringLiteral("gaia_dr3_g14"),
            .displayName = QStringLiteral("Gaia DR3 G<=14"),
            .description = QStringLiteral("Recommended starter pack (~17M stars, G<=14). "
                                          "Balanced depth and storage for plate solving and photometric workflows."),
            .manifestUrl = QUrl(base_url + QStringLiteral("/gaia_dr3_g14/catalog_meta.json")),
            .distributionType = QStringLiteral("chunks"),
            .recommended = true,
            .downloadSizeBytes = 1028LL * 1024LL * 1024LL,
            .installedSizeBytes = 1028LL * 1024LL * 1024LL,
        },
        CatalogPackDescriptor{
            .id = QStringLiteral("gaia_dr3_g16"),
            .displayName = QStringLiteral("Gaia DR3 G<=16"),
            .description = QStringLiteral("Deep all-sky catalog (~78M stars, G<=16). "
                                          "Useful for narrow or sparse fields, but substantially larger."),
            .manifestUrl = QUrl(base_url + QStringLiteral("/gaia_dr3_g16/catalog_meta.json")),
            .distributionType = QStringLiteral("chunks"),
            .recommended = false,
            .downloadSizeBytes = 4758LL * 1024LL * 1024LL,
            .installedSizeBytes = 4758LL * 1024LL * 1024LL,
        },
        CatalogPackDescriptor{
            .id = QStringLiteral("gaia_dr3_g18"),
            .displayName = QStringLiteral("Gaia DR3 G<=18"),
            .description = QStringLiteral("Very deep all-sky catalog (~305M stars, G<=18). "
                                          "For long focal lengths and extremely sparse fields. Large download."),
            .manifestUrl = QUrl(base_url + QStringLiteral("/gaia_dr3_g18/catalog_meta.json")),
            .distributionType = QStringLiteral("chunks"),
            .recommended = false,
            .downloadSizeBytes = 18LL * 1024LL * 1024LL * 1024LL,
            .installedSizeBytes = 18LL * 1024LL * 1024LL * 1024LL,
        },
    };
}

[[nodiscard]] inline QString resolveGaiaCatalogPack(GlobalSettingsPersistence* settings)
{
    if (!settings)
    {
        return defaultGaiaCatalogPack();
    }

    const QString configured_pack = settings
                                        ->select(SettingsKeys::PLATESOLVE_GAIA_CATALOG_PACK,
                                                 QString(SettingsDefaults::PLATESOLVE_GAIA_CATALOG_PACK))
                                        .trimmed();
    return configured_pack.isEmpty() ? defaultGaiaCatalogPack() : configured_pack;
}

[[nodiscard]] inline QString resolveGaiaCatalogDirectory(GlobalSettingsPersistence* settings)
{
    if (!settings)
    {
        return defaultGaiaCatalogDirectory();
    }

    const QString configured_directory = settings
                                             ->select(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY,
                                                      QString(SettingsDefaults::PLATESOLVE_GAIA_CATALOG_DIRECTORY))
                                             .trimmed();
    const QString configured_pack = resolveGaiaCatalogPack(settings);
    if (!configured_directory.isEmpty())
    {
        const GaiaCatalogDirectoryInspection inspection = inspectGaiaCatalogDirectoryDetailed(configured_directory);
        if (inspection.status != GaiaCatalogDirectoryStatus::Missing &&
            inspection.status != GaiaCatalogDirectoryStatus::Invalid)
        {
            return configured_directory;
        }

        const QVector<GaiaCatalogPackLocation> packs = scanGaiaCatalogPacks(configured_directory);
        for (const GaiaCatalogPackLocation& pack : packs)
        {
            if (pack.packId == configured_pack)
            {
                return pack.directoryPath;
            }
        }
        if (packs.size() == 1)
        {
            return packs.front().directoryPath;
        }

        return configured_directory;
    }

    return QDir(CatalogDownloadService::defaultCatalogRootDirectory()).filePath(configured_pack);
}

[[nodiscard]] inline QString resolveGaiaCatalogPack(IGlobalSettingsProvider* settings)
{
    if (!settings)
    {
        return defaultGaiaCatalogPack();
    }

    const QString configured_pack = settings
                                        ->select(SettingsKeys::PLATESOLVE_GAIA_CATALOG_PACK,
                                                 QString(SettingsDefaults::PLATESOLVE_GAIA_CATALOG_PACK))
                                        .trimmed();
    return configured_pack.isEmpty() ? defaultGaiaCatalogPack() : configured_pack;
}

[[nodiscard]] inline QString resolveGaiaCatalogDirectory(IGlobalSettingsProvider* settings)
{
    if (!settings)
    {
        return defaultGaiaCatalogDirectory();
    }

    const QString configured_directory = settings
                                             ->select(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY,
                                                      QString(SettingsDefaults::PLATESOLVE_GAIA_CATALOG_DIRECTORY))
                                             .trimmed();
    const QString configured_pack = resolveGaiaCatalogPack(settings);
    if (!configured_directory.isEmpty())
    {
        const GaiaCatalogDirectoryInspection inspection = inspectGaiaCatalogDirectoryDetailed(configured_directory);
        if (inspection.status != GaiaCatalogDirectoryStatus::Missing &&
            inspection.status != GaiaCatalogDirectoryStatus::Invalid)
        {
            return configured_directory;
        }

        const QVector<GaiaCatalogPackLocation> packs = scanGaiaCatalogPacks(configured_directory);
        for (const GaiaCatalogPackLocation& pack : packs)
        {
            if (pack.packId == configured_pack)
            {
                return pack.directoryPath;
            }
        }
        if (packs.size() == 1)
        {
            return packs.front().directoryPath;
        }

        return configured_directory;
    }

    return QDir(CatalogDownloadService::defaultCatalogRootDirectory()).filePath(configured_pack);
}

[[nodiscard]] inline GaiaCatalogDirectoryStatus inspectGaiaCatalogDirectory(GlobalSettingsPersistence* settings)
{
    return inspectGaiaCatalogDirectory(resolveGaiaCatalogDirectory(settings));
}

[[nodiscard]] inline QString resolveGaiaCatalogDirectory(const PlateSolveSettings& settings)
{
    const QString configured_directory = settings.gaiaCatalogDirectory.trimmed();
    const QString pack =
        settings.gaiaCatalogPack.trimmed().isEmpty() ? defaultGaiaCatalogPack() : settings.gaiaCatalogPack.trimmed();
    if (!configured_directory.isEmpty())
    {
        const GaiaCatalogDirectoryInspection inspection = inspectGaiaCatalogDirectoryDetailed(configured_directory);
        if (inspection.status != GaiaCatalogDirectoryStatus::Missing &&
            inspection.status != GaiaCatalogDirectoryStatus::Invalid)
        {
            return configured_directory;
        }

        const QVector<GaiaCatalogPackLocation> packs = scanGaiaCatalogPacks(configured_directory);
        for (const GaiaCatalogPackLocation& discoveredPack : packs)
        {
            if (discoveredPack.packId == pack)
            {
                return discoveredPack.directoryPath;
            }
        }
        if (packs.size() == 1)
        {
            return packs.front().directoryPath;
        }

        return configured_directory;
    }

    return QDir(CatalogDownloadService::defaultCatalogRootDirectory()).filePath(pack);
}

[[nodiscard]] inline GaiaCatalogDirectoryStatus inspectGaiaCatalogDirectory(const PlateSolveSettings& settings)
{
    return inspectGaiaCatalogDirectory(resolveGaiaCatalogDirectory(settings));
}

[[nodiscard]] inline bool isGaiaCatalogReady(const PlateSolveSettings& settings)
{
    return inspectGaiaCatalogDirectory(settings) == GaiaCatalogDirectoryStatus::Ready;
}

} // namespace an::ui
