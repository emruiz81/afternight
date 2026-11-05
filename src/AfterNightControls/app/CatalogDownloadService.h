// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include "app/SpccResponseSettings.h"

#include <AfterNightEngine/Core/Result.h>

#include <QObject>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVector>
#include <verdigris/wobjectdefs.h>

#include <cstdint>
#include <memory>
#include <vector>

class QNetworkReply;

namespace an::ui
{

struct CatalogPackDescriptor
{
    QString id;
    QString displayName;
    QString description;
    QUrl manifestUrl;
    QString catalogMetaSha256;
    QString distributionType;
    QString catalogFamilyId;
    QString accessProfile;
    QString rightsReviewStatus;
    QString releaseStatus;
    bool recommended = false;
    qint64 downloadSizeBytes = 0;
    qint64 installedSizeBytes = 0;
    bool hasRegionCoverage = false;
    double centerRaDeg = 0.0;
    double centerDecDeg = 0.0;
    double coverageRadiusDeg = 0.0;
    double magnitudeLimit = 0.0;
};

[[nodiscard]] inline bool isUsableCatalogManifestUrl(const QUrl& manifestUrl)
{
    return !manifestUrl.isEmpty() && manifestUrl.isValid() && !manifestUrl.isRelative();
}

struct CatalogDownloadProgress
{
    QString packId;
    QString phase; // "manifest", "download", "verify"
    qint64 bytesReceived = 0;
    qint64 bytesTotal = 0;
    int filesCompleted = 0;
    int filesTotal = 0;
    QString currentFile;
};

struct CatalogDownloadResult
{
    bool success = false;
    bool cancelled = false;
    QString packId;
    QString installDirectory;
    QString errorMessage;
};

enum class SpccCatalogChunkDownloadMode
{
    RequiredRegion,
    FullCatalog,
};

struct SpccCatalogChunkDownloadRequest
{
    CatalogPackDescriptor pack;
    QString installRootDirectory;
    SpccCatalogChunkDownloadMode mode = SpccCatalogChunkDownloadMode::RequiredRegion;
    bool hasRegion = false;
    double centerRaDeg = 0.0;
    double centerDecDeg = 0.0;
    double radiusDeg = 0.0;
    std::vector<std::uint64_t> sourceIds;
};

class AFTERNIGHTCONTROLS_API CatalogDownloadService : public QObject
{
    W_OBJECT(CatalogDownloadService)

public:
    explicit CatalogDownloadService(QObject* parent = nullptr);
    ~CatalogDownloadService() override;

    [[nodiscard]] static QString defaultCatalogRootDirectory();

    void setCatalogRootDirectory(const QString& path);
    [[nodiscard]] QString catalogRootDirectory() const;
    void setSpccResponseRepositoryRootDirectory(const QString& path);
    [[nodiscard]] QString spccResponseRepositoryRootDirectory() const;
    [[nodiscard]] bool isBusy() const;

    void downloadCatalogPack(const CatalogPackDescriptor& pack);
    void downloadSpccCatalogChunks(const SpccCatalogChunkDownloadRequest& request);
    void downloadSpccResponseRepository(const SpccResponseRepositoryDescriptor& repository);
    void cancel();

    void downloadProgress(CatalogDownloadProgress progress) W_SIGNAL(downloadProgress, progress);
    void downloadFinished(CatalogDownloadResult result) W_SIGNAL(downloadFinished, result);

private:
    void handleReplyFinished();
    void handleReplyReadyRead();
    void handleReplyDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void startManifestDownload();
    bool startChunkDownload(int chunkIndex);
    void processPendingChunkVerifications();
    void continueChunkVerification();
    void handleManifestDownloaded();

    /// Asks the engine on a worker which SPCC catalog chunks the active request needs, then goes on downloading.
    void selectSpccChunksThenDownload();

    /// Keeps the chunks the selection names and starts downloading them, or finishes with why it cannot.
    void continueAfterSpccChunkSelection(const an::Result<QSet<int>>& selected);

    /// Starts downloading the manifest's chunks from the first.
    void startChunkDownloads();
    void handleChunkDownloaded(QNetworkReply* reply);
    void processNextChunk();
    void emitProgress(const QString& phase, qint64 bytesReceived, qint64 bytesTotal, int filesCompleted = 0,
                      int filesTotal = 0, const QString& currentFile = QString());
    void finishWithResult(CatalogDownloadResult result);

    struct impl;
    std::unique_ptr<impl> m_impl;
};

} // namespace an::ui

Q_DECLARE_METATYPE(an::ui::CatalogPackDescriptor)
Q_DECLARE_METATYPE(an::ui::CatalogDownloadProgress)
Q_DECLARE_METATYPE(an::ui::CatalogDownloadResult)
W_REGISTER_ARGTYPE(an::ui::CatalogDownloadProgress)
W_REGISTER_ARGTYPE(an::ui::CatalogDownloadResult)
