// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The catalog downloads: a Gaia chunk catalog, an SPCC spectrum catalog - whole, or only the chunks a target region
/// needs, which the engine selects on a worker - and an SPCC response repository, each fetched from a manifest over a
/// local file or a local HTTP service, verified, and installed whole or not at all.

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include <gtest/gtest.h>

#include <QtTest/QSignalSpy>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QSet>
#include <QStringList>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include "app/CatalogDownloadService.h"
#include "app/SpccCatalogSettings.h"
#include "app/SpccResponseSettings.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

namespace
{

struct CatalogSourceFixture
{
    an::ui::CatalogPackDescriptor pack;
    QString manifestPath;
    QString chunk0Path;
    QString chunk1Path;
    QByteArray chunk0Bytes;
    QByteArray chunk1Bytes;
};

struct SpccCatalogSourceFixture
{
    an::ui::CatalogPackDescriptor pack;
    QString manifestId;
    QString manifestPath;
    QString chunkPath;
    QString chunkManifestPath;
    QString coverageSummaryPath;
    QString sourceIdIndexPath;
    QString sourceCountTranscriptPath;
    QString regionalRequestPath;
    QByteArray chunkBytes;
    QByteArray chunkManifestBytes;
    QByteArray coverageSummaryBytes;
    QByteArray sourceIdIndexBytes;
    QByteArray sourceCountTranscriptBytes;
    QByteArray regionalRequestBytes;
};

struct SpccResponseRepositorySourceFixture
{
    an::ui::SpccResponseRepositoryDescriptor descriptor;
    QString manifestPath;
    QString redPath;
    QString whitePath;
    QByteArray manifestBytes;
    QByteArray redBytes;
    QByteArray whiteBytes;
};

class LocalHttpFileServer
{
public:
    explicit LocalHttpFileServer(QString rootPath, QSet<QString> delayedPaths = {}, const int responseDelayMs = 0)
        : m_rootPath(std::move(rootPath)), m_delayedPaths(std::move(delayedPaths)), m_responseDelayMs(responseDelayMs)
    {
        EXPECT_TRUE(m_server.listen(QHostAddress::LocalHost, 0)) << m_server.errorString().toStdString();
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] { acceptPendingConnections(); });
    }

    [[nodiscard]] QUrl urlFor(const QString& relativePath) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/%2").arg(m_server.serverPort()).arg(relativePath));
    }

    [[nodiscard]] QStringList requestedPaths() const
    {
        return m_requestedPaths;
    }

    [[nodiscard]] int maxInFlightRequests() const
    {
        return m_maxInFlightRequests;
    }

private:
    void acceptPendingConnections()
    {
        while (m_server.hasPendingConnections())
        {
            QTcpSocket* socket = m_server.nextPendingConnection();
            if (!socket)
            {
                continue;
            }

            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

            auto requestBytes = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, requestBytes] {
                requestBytes->append(socket->readAll());
                if (!requestBytes->contains("\r\n\r\n"))
                {
                    return;
                }

                serveRequest(socket, *requestBytes);
            });
        }
    }

    void serveRequest(QTcpSocket* socket, const QByteArray& requestBytes)
    {
        const QList<QByteArray> lines = requestBytes.split('\n');
        const QList<QByteArray> parts = lines.isEmpty() ? QList<QByteArray>{} : lines.front().trimmed().split(' ');
        if (parts.size() < 2 || parts.front() != "GET")
        {
            writeResponseLater(socket, QByteArrayLiteral("405 Method Not Allowed"), QByteArray(),
                               QByteArrayLiteral("text/plain"), QString());
            return;
        }

        const QUrl requestUrl(QString::fromLatin1(parts.at(1)));
        const QString cleanPath = QDir::cleanPath(requestUrl.path().mid(1));
        m_requestedPaths.push_back(QStringLiteral("/") + cleanPath);
        if (cleanPath.isEmpty() || cleanPath == QStringLiteral(".") || cleanPath == QStringLiteral("..") ||
            cleanPath.startsWith(QStringLiteral("../")) || cleanPath.contains(QStringLiteral("/../")))
        {
            writeResponseLater(socket, QByteArrayLiteral("404 Not Found"), QByteArray(),
                               QByteArrayLiteral("text/plain"), cleanPath);
            return;
        }

        QFile file(QDir(m_rootPath).filePath(cleanPath));
        if (!file.open(QIODevice::ReadOnly))
        {
            writeResponseLater(socket, QByteArrayLiteral("404 Not Found"), QByteArray(),
                               QByteArrayLiteral("text/plain"), cleanPath);
            return;
        }

        const QByteArray body = file.readAll();
        const QByteArray contentType = cleanPath.endsWith(QStringLiteral(".json"))
                                           ? QByteArrayLiteral("application/json")
                                           : QByteArrayLiteral("application/octet-stream");
        writeResponseLater(socket, QByteArrayLiteral("200 OK"), body, contentType, cleanPath);
    }

    void writeResponseLater(QTcpSocket* socket, QByteArray status, QByteArray body, QByteArray contentType,
                            const QString& cleanPath)
    {
        ++m_inFlightRequests;
        m_maxInFlightRequests = (std::max)(m_maxInFlightRequests, m_inFlightRequests);

        auto sendResponse = [this, socket, status = std::move(status), body = std::move(body),
                             contentType = std::move(contentType)]() mutable {
            writeResponse(socket, status, body, contentType);
            --m_inFlightRequests;
        };

        const QString requestPath = QStringLiteral("/") + cleanPath;
        const bool delayed = m_responseDelayMs > 0 && (m_delayedPaths.isEmpty() || m_delayedPaths.contains(cleanPath) ||
                                                       m_delayedPaths.contains(requestPath));
        if (delayed)
        {
            QTimer::singleShot(m_responseDelayMs, &m_server, std::move(sendResponse));
            return;
        }

        sendResponse();
    }

    static void writeResponse(QTcpSocket* socket, const QByteArray& status, const QByteArray& body,
                              const QByteArray& contentType)
    {
        QByteArray response;
        response += "HTTP/1.1 " + status + "\r\n";
        response += "Content-Type: " + contentType + "\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        response += "Connection: close\r\n\r\n";
        response += body;
        socket->write(response);
        socket->disconnectFromHost();
    }

    QString m_rootPath;
    QSet<QString> m_delayedPaths;
    int m_responseDelayMs = 0;
    int m_inFlightRequests = 0;
    int m_maxInFlightRequests = 0;
    QTcpServer m_server;
    QStringList m_requestedPaths;
};

class StandardPathsTestMode final
{
public:
    StandardPathsTestMode()
    {
        QStandardPaths::setTestModeEnabled(true);
        QFile::remove(an::ui::spccCatalogMirrorConfigFilePath());
    }

    ~StandardPathsTestMode()
    {
        QFile::remove(an::ui::spccCatalogMirrorConfigFilePath());
        QStandardPaths::setTestModeEnabled(false);
    }
};

QByteArray sha256Hex(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

bool writeBytes(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).dir().path());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return false;
    }
    if (file.write(data) != data.size())
    {
        return false;
    }
    file.close();
    return true;
}

CatalogSourceFixture createCatalogSource(QTemporaryDir& sourceDir, const bool corruptFirstChunkHash = false)
{
    CatalogSourceFixture fixture;
    fixture.pack.id = QStringLiteral("gaia_dr3_g14");
    fixture.pack.displayName = QStringLiteral("Gaia DR3 G<=14");
    fixture.pack.manifestUrl =
        QUrl::fromLocalFile(QDir(sourceDir.path()).filePath(QStringLiteral("catalog_meta.json")));
    fixture.pack.distributionType = QStringLiteral("chunks");
    fixture.chunk0Bytes = QByteArray("chunk-zero-data");
    fixture.chunk1Bytes = QByteArray("chunk-one-data");
    fixture.chunk0Path = QDir(sourceDir.path()).filePath(QStringLiteral("gaia_dr3_g14_chunk_00.ansp"));
    fixture.chunk1Path = QDir(sourceDir.path()).filePath(QStringLiteral("gaia_dr3_g14_chunk_01.ansp"));
    fixture.manifestPath = fixture.pack.manifestUrl.toLocalFile();

    EXPECT_TRUE(writeBytes(fixture.chunk0Path, fixture.chunk0Bytes));
    EXPECT_TRUE(writeBytes(fixture.chunk1Path, fixture.chunk1Bytes));

    QJsonObject root;
    root.insert(QStringLiteral("format_version"), 3);
    root.insert(QStringLiteral("storage"), QStringLiteral("chunks"));
    root.insert(QStringLiteral("catalog_id"), fixture.pack.id);
    root.insert(QStringLiteral("chunk_count"), 2);
    root.insert(
        QStringLiteral("chunks"),
        QJsonArray{
            QJsonObject{
                {QStringLiteral("index"), 0},
                {QStringLiteral("file"), QStringLiteral("gaia_dr3_g14_chunk_00.ansp")},
                {QStringLiteral("first_tile"), 0},
                {QStringLiteral("tile_count"), 256},
                {QStringLiteral("stars"), 1},
                {QStringLiteral("size_bytes"), static_cast<double>(fixture.chunk0Bytes.size())},
                {QStringLiteral("sha256"),
                 QString::fromLatin1(corruptFirstChunkHash ? QByteArray(64, '0') : sha256Hex(fixture.chunk0Bytes))},
            },
            QJsonObject{
                {QStringLiteral("index"), 1},
                {QStringLiteral("file"), QStringLiteral("gaia_dr3_g14_chunk_01.ansp")},
                {QStringLiteral("first_tile"), 256},
                {QStringLiteral("tile_count"), 256},
                {QStringLiteral("stars"), 1},
                {QStringLiteral("size_bytes"), static_cast<double>(fixture.chunk1Bytes.size())},
                {QStringLiteral("sha256"), QString::fromLatin1(sha256Hex(fixture.chunk1Bytes))},
            },
        });

    EXPECT_TRUE(writeBytes(fixture.manifestPath, QJsonDocument(root).toJson(QJsonDocument::Compact)));

    fixture.pack.downloadSizeBytes = fixture.chunk0Bytes.size() + fixture.chunk1Bytes.size();
    fixture.pack.installedSizeBytes = fixture.pack.downloadSizeBytes;
    return fixture;
}

QByteArray compactJson(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

SpccCatalogSourceFixture
createSpccCatalogSource(QTemporaryDir& sourceDir,
                        const QString& manifestAccessProfile = QStringLiteral("installed-regional-cache"),
                        const QString& manifestRightsReviewStatus = QStringLiteral("approved-for-managed-download"),
                        const bool corruptChunkHash = false, const bool includeRegionalRequestSidecar = false)
{
    constexpr auto kCatalogFamilyId = "spcc_gaia_dr3_xp_sampled";
    constexpr auto kChunkFile = "chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp";

    SpccCatalogSourceFixture fixture;
    fixture.manifestId = QStringLiteral("spcc-gaia-dr3-xp-sampled-download-test-2026a");
    fixture.pack.id = fixture.manifestId;
    fixture.pack.displayName = QStringLiteral("SPCC Gaia DR3 XP Sampled Test");
    fixture.pack.manifestUrl =
        QUrl::fromLocalFile(QDir(sourceDir.path()).filePath(QStringLiteral("catalog_meta.json")));
    fixture.pack.distributionType = QStringLiteral("chunks");
    fixture.pack.catalogFamilyId = QString::fromLatin1(kCatalogFamilyId);
    fixture.pack.accessProfile = QStringLiteral("installed-regional-cache");
    fixture.pack.rightsReviewStatus = QStringLiteral("approved-for-managed-download");
    fixture.chunkBytes = QByteArray("spcc-anxp-chunk-data");
    fixture.chunkPath = QDir(sourceDir.path()).filePath(QString::fromLatin1(kChunkFile));
    fixture.chunkManifestPath = QDir(sourceDir.path()).filePath(QStringLiteral("chunk_manifest.json"));
    fixture.coverageSummaryPath = QDir(sourceDir.path()).filePath(QStringLiteral("coverage_summary.json"));
    fixture.sourceIdIndexPath = QDir(sourceDir.path()).filePath(QStringLiteral("source_id_index.json"));
    fixture.sourceCountTranscriptPath = QDir(sourceDir.path()).filePath(QStringLiteral("source_count_transcript.json"));
    fixture.regionalRequestPath = QDir(sourceDir.path()).filePath(QStringLiteral("regional_cache_request.json"));
    fixture.manifestPath = fixture.pack.manifestUrl.toLocalFile();

    fixture.chunkManifestBytes = compactJson(QJsonObject{
        {QStringLiteral("catalog_family_id"), QString::fromLatin1(kCatalogFamilyId)},
        {QStringLiteral("manifest_id"), fixture.manifestId},
        {QStringLiteral("chunks"), QJsonArray{QJsonObject{
                                       {QStringLiteral("chunk_index"), 0},
                                       {QStringLiteral("file"), QString::fromLatin1(kChunkFile)},
                                       {QStringLiteral("record_count"), 1},
                                   }}},
    });
    fixture.coverageSummaryBytes = compactJson(QJsonObject{
        {QStringLiteral("catalog_family_id"), QString::fromLatin1(kCatalogFamilyId)},
        {QStringLiteral("manifest_id"), fixture.manifestId},
        {QStringLiteral("total_records"), 1},
    });
    fixture.sourceIdIndexBytes = compactJson(QJsonObject{
        {QStringLiteral("catalog_family_id"), QString::fromLatin1(kCatalogFamilyId)},
        {QStringLiteral("manifest_id"), fixture.manifestId},
        {QStringLiteral("source_count"), 1},
        {QStringLiteral("sources"), QJsonObject{}},
    });
    fixture.sourceCountTranscriptBytes = compactJson(QJsonObject{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-source-count-transcript-test-v1")},
        {QStringLiteral("manifest_id"), fixture.manifestId},
    });
    fixture.regionalRequestBytes = compactJson(QJsonObject{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-regional-cache-request-v1")},
        {QStringLiteral("catalog_family_id"), QString::fromLatin1(kCatalogFamilyId)},
        {QStringLiteral("access_profile"), QStringLiteral("installed-regional-cache")},
        {QStringLiteral("target_name"), QStringLiteral("M31 Core")},
        {QStringLiteral("created_at_utc"), QStringLiteral("2026-05-12T00:00:00Z")},
        {QStringLiteral("region"),
         QJsonObject{
             {QStringLiteral("center_ra_deg"), 10.6847083},
             {QStringLiteral("center_dec_deg"), 41.26875},
             {QStringLiteral("radius_deg"), 1.5},
         }},
        {QStringLiteral("selection"), QJsonObject{{QStringLiteral("magnitude_limit"), 16.0}}},
        {QStringLiteral("image"),
         QJsonObject{
             {QStringLiteral("width"), 6248},
             {QStringLiteral("height"), 4176},
         }},
    });

    EXPECT_TRUE(writeBytes(fixture.chunkPath, fixture.chunkBytes));
    EXPECT_TRUE(writeBytes(fixture.chunkManifestPath, fixture.chunkManifestBytes));
    EXPECT_TRUE(writeBytes(fixture.coverageSummaryPath, fixture.coverageSummaryBytes));
    EXPECT_TRUE(writeBytes(fixture.sourceIdIndexPath, fixture.sourceIdIndexBytes));
    EXPECT_TRUE(writeBytes(fixture.sourceCountTranscriptPath, fixture.sourceCountTranscriptBytes));
    if (includeRegionalRequestSidecar)
    {
        EXPECT_TRUE(writeBytes(fixture.regionalRequestPath, fixture.regionalRequestBytes));
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), QStringLiteral("spcc-catalog-meta-v1"));
    root.insert(QStringLiteral("catalog_family_id"), QString::fromLatin1(kCatalogFamilyId));
    root.insert(QStringLiteral("manifest_id"), fixture.manifestId);
    root.insert(QStringLiteral("access_profile"), manifestAccessProfile);
    root.insert(QStringLiteral("rights"), QJsonObject{
                                              {QStringLiteral("review_status"), manifestRightsReviewStatus},
                                          });
    root.insert(QStringLiteral("total_records"), 1);
    QJsonObject files{
        {QStringLiteral("chunk_manifest"), QStringLiteral("chunk_manifest.json")},
        {QStringLiteral("coverage_summary"), QStringLiteral("coverage_summary.json")},
        {QStringLiteral("source_id_index"), QStringLiteral("source_id_index.json")},
        {QStringLiteral("source_count_transcript"), QStringLiteral("source_count_transcript.json")},
    };
    if (includeRegionalRequestSidecar)
    {
        files.insert(QStringLiteral("regional_cache_request"), QStringLiteral("regional_cache_request.json"));
    }
    root.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("chunk_manifest_sha256"), QString::fromLatin1(sha256Hex(fixture.chunkManifestBytes)));
    root.insert(QStringLiteral("coverage_summary_sha256"),
                QString::fromLatin1(sha256Hex(fixture.coverageSummaryBytes)));
    root.insert(QStringLiteral("source_id_index_sha256"), QString::fromLatin1(sha256Hex(fixture.sourceIdIndexBytes)));
    root.insert(QStringLiteral("source_count_transcript_sha256"),
                QString::fromLatin1(sha256Hex(fixture.sourceCountTranscriptBytes)));
    if (includeRegionalRequestSidecar)
    {
        root.insert(QStringLiteral("regional_cache_request_sha256"),
                    QString::fromLatin1(sha256Hex(fixture.regionalRequestBytes)));
    }
    root.insert(QStringLiteral("chunks"),
                QJsonArray{QJsonObject{
                    {QStringLiteral("chunk_index"), 0},
                    {QStringLiteral("file"), QString::fromLatin1(kChunkFile)},
                    {QStringLiteral("first_tile"), 0},
                    {QStringLiteral("tile_count"), 64},
                    {QStringLiteral("record_count"), 1},
                    {QStringLiteral("size_bytes"), static_cast<double>(fixture.chunkBytes.size())},
                    {QStringLiteral("sha256"),
                     QString::fromLatin1(corruptChunkHash ? QByteArray(64, '0') : sha256Hex(fixture.chunkBytes))},
                }});

    const QByteArray manifestBytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    EXPECT_TRUE(writeBytes(fixture.manifestPath, manifestBytes));
    fixture.pack.catalogMetaSha256 = QString::fromLatin1(sha256Hex(manifestBytes));

    fixture.pack.downloadSizeBytes = fixture.chunkBytes.size();
    fixture.pack.installedSizeBytes = fixture.pack.downloadSizeBytes;
    return fixture;
}

QByteArray addSecondSpccChunkForM31(QTemporaryDir& sourceDir, SpccCatalogSourceFixture& fixture)
{
    const QByteArray chunkBytes("spcc-anxp-m31-chunk-data");
    const QString chunkPath =
        QDir(sourceDir.path()).filePath(QStringLiteral("chunks/spcc_gaia_dr3_xp_sampled_chunk_0001.anxp"));
    EXPECT_TRUE(writeBytes(chunkPath, chunkBytes));

    QFile manifestFile(fixture.manifestPath);
    EXPECT_TRUE(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();

    QJsonArray chunks = root.value(QStringLiteral("chunks")).toArray();
    chunks.append(QJsonObject{
        {QStringLiteral("chunk_index"), 1},
        {QStringLiteral("file"), QStringLiteral("chunks/spcc_gaia_dr3_xp_sampled_chunk_0001.anxp")},
        {QStringLiteral("first_tile"), 2688},
        {QStringLiteral("tile_count"), 64},
        {QStringLiteral("record_count"), 1},
        {QStringLiteral("size_bytes"), static_cast<double>(chunkBytes.size())},
        {QStringLiteral("sha256"), QString::fromLatin1(sha256Hex(chunkBytes))},
    });
    root.insert(QStringLiteral("chunks"), chunks);
    const QByteArray manifestBytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    EXPECT_TRUE(writeBytes(fixture.manifestPath, manifestBytes));
    fixture.pack.catalogMetaSha256 = QString::fromLatin1(sha256Hex(manifestBytes));
    fixture.pack.downloadSizeBytes += chunkBytes.size();
    fixture.pack.installedSizeBytes = fixture.pack.downloadSizeBytes;
    return chunkBytes;
}

SpccResponseRepositorySourceFixture createSpccResponseRepositorySource(QTemporaryDir& sourceDir,
                                                                       const bool corruptComponentHash = false,
                                                                       const QString& componentReviewStatus = QString())
{
    SpccResponseRepositorySourceFixture fixture;
    fixture.descriptor.id = QStringLiteral("spcc-response-download-fixture-2026a");
    fixture.descriptor.repositoryId = QStringLiteral("spcc-response-download-fixture");
    fixture.descriptor.version = QStringLiteral("2026a");
    fixture.descriptor.displayName = QStringLiteral("SPCC Response Download Fixture");
    fixture.descriptor.manifestUrl =
        QUrl::fromLocalFile(QDir(sourceDir.path()).filePath(QStringLiteral("response_meta.json")));
    fixture.descriptor.rightsReviewStatus = QStringLiteral("approved-for-managed-download");
    fixture.descriptor.deploymentScope = QStringLiteral("managed-cache");
    const QString componentReview = componentReviewStatus.trimmed().isEmpty() ? fixture.descriptor.rightsReviewStatus
                                                                              : componentReviewStatus.trimmed();

    fixture.redBytes = compactJson(QJsonObject{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-response-asset-v1")},
        {QStringLiteral("asset_kind"), QStringLiteral("component")},
        {QStringLiteral("component_id"), QStringLiteral("sensor.synthetic.r")},
        {QStringLiteral("version"), QStringLiteral("1")},
        {QStringLiteral("role"), QStringLiteral("sensor-red-response")},
        {QStringLiteral("wavelength_unit"), QStringLiteral("nm")},
        {QStringLiteral("value_semantics"), QStringLiteral("dimensionless-throughput")},
        {QStringLiteral("review_status"), componentReview},
        {QStringLiteral("license_name"), QStringLiteral("synthetic-fixture")},
        {QStringLiteral("source_url"), QStringLiteral("local://synthetic/sensor-r")},
        {QStringLiteral("data_sha256"), QStringLiteral("red-data-sha")},
        {QStringLiteral("samples"),
         QJsonArray{
             QJsonObject{{QStringLiteral("wavelength_nm"), 400.0}, {QStringLiteral("value"), 0.1}},
             QJsonObject{{QStringLiteral("wavelength_nm"), 700.0}, {QStringLiteral("value"), 0.8}},
         }},
    });
    fixture.whiteBytes = compactJson(QJsonObject{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-response-asset-v1")},
        {QStringLiteral("asset_kind"), QStringLiteral("white_reference")},
        {QStringLiteral("white_reference_id"), QStringLiteral("white.synthetic.d65")},
        {QStringLiteral("version"), QStringLiteral("1")},
        {QStringLiteral("wavelength_unit"), QStringLiteral("nm")},
        {QStringLiteral("value_semantics"), QStringLiteral("relative-sed")},
        {QStringLiteral("review_status"), QStringLiteral("approved-for-managed-download")},
        {QStringLiteral("license_name"), QStringLiteral("synthetic-fixture")},
        {QStringLiteral("source_url"), QStringLiteral("local://synthetic/white-d65")},
        {QStringLiteral("data_sha256"), QStringLiteral("white-data-sha")},
        {QStringLiteral("samples"),
         QJsonArray{
             QJsonObject{{QStringLiteral("wavelength_nm"), 400.0}, {QStringLiteral("value"), 1.0}},
             QJsonObject{{QStringLiteral("wavelength_nm"), 700.0}, {QStringLiteral("value"), 1.0}},
         }},
    });

    fixture.redPath = QDir(sourceDir.path()).filePath(QStringLiteral("components/sensor_synthetic_r.json"));
    fixture.whitePath = QDir(sourceDir.path()).filePath(QStringLiteral("white_references/white_synthetic_d65.json"));
    fixture.manifestPath = fixture.descriptor.manifestUrl.toLocalFile();

    EXPECT_TRUE(writeBytes(fixture.redPath, fixture.redBytes));
    EXPECT_TRUE(writeBytes(fixture.whitePath, fixture.whiteBytes));

    const QByteArray expectedRedSha = corruptComponentHash ? QByteArray(64, '0') : sha256Hex(fixture.redBytes);
    const QJsonObject root{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-response-repository-v1")},
        {QStringLiteral("repository_id"), fixture.descriptor.repositoryId},
        {QStringLiteral("version"), fixture.descriptor.version},
        {QStringLiteral("rights"),
         QJsonObject{
             {QStringLiteral("review_status"), fixture.descriptor.rightsReviewStatus},
             {QStringLiteral("license_name"), QStringLiteral("synthetic-fixture")},
         }},
        {QStringLiteral("deployment_scopes"), QJsonArray{QStringLiteral("managed-cache")}},
        {QStringLiteral("components"),
         QJsonArray{QJsonObject{
             {QStringLiteral("component_id"), QStringLiteral("sensor.synthetic.r")},
             {QStringLiteral("version"), QStringLiteral("1")},
             {QStringLiteral("role"), QStringLiteral("sensor-red-response")},
             {QStringLiteral("file"), QStringLiteral("components/sensor_synthetic_r.json")},
             {QStringLiteral("wavelength_unit"), QStringLiteral("nm")},
             {QStringLiteral("value_semantics"), QStringLiteral("dimensionless-throughput")},
             {QStringLiteral("review_status"), componentReview},
             {QStringLiteral("license_name"), QStringLiteral("synthetic-fixture")},
             {QStringLiteral("data_sha256"), QStringLiteral("red-data-sha")},
             {QStringLiteral("file_sha256"), QString::fromLatin1(expectedRedSha)},
         }}},
        {QStringLiteral("white_references"),
         QJsonArray{QJsonObject{
             {QStringLiteral("white_reference_id"), QStringLiteral("white.synthetic.d65")},
             {QStringLiteral("version"), QStringLiteral("1")},
             {QStringLiteral("file"), QStringLiteral("white_references/white_synthetic_d65.json")},
             {QStringLiteral("wavelength_unit"), QStringLiteral("nm")},
             {QStringLiteral("value_semantics"), QStringLiteral("relative-sed")},
             {QStringLiteral("review_status"), fixture.descriptor.rightsReviewStatus},
             {QStringLiteral("license_name"), QStringLiteral("synthetic-fixture")},
             {QStringLiteral("data_sha256"), QStringLiteral("white-data-sha")},
             {QStringLiteral("file_sha256"), QString::fromLatin1(sha256Hex(fixture.whiteBytes))},
         }}},
        {QStringLiteral("throughput_profiles"),
         QJsonArray{QJsonObject{
             {QStringLiteral("profile_id"), QStringLiteral("profile.synthetic.rgb")},
             {QStringLiteral("version"), QStringLiteral("1")},
             {QStringLiteral("component_ids"), QJsonArray{QStringLiteral("sensor.synthetic.r")}},
             {QStringLiteral("white_reference_id"), QStringLiteral("white.synthetic.d65")},
             {QStringLiteral("compatible_channel_bases"),
              QJsonArray{QStringLiteral("camera-rgb"), QStringLiteral("linear-rgb")}},
             {QStringLiteral("models_raw_white_balance"), false},
             {QStringLiteral("models_camera_matrix"), false},
         }}},
    };
    fixture.manifestBytes = compactJson(root);
    EXPECT_TRUE(writeBytes(fixture.manifestPath, fixture.manifestBytes));
    fixture.descriptor.responseMetaSha256 = QString::fromLatin1(sha256Hex(fixture.manifestBytes));
    fixture.descriptor.downloadSizeBytes =
        fixture.manifestBytes.size() + fixture.redBytes.size() + fixture.whiteBytes.size();
    fixture.descriptor.installedSizeBytes = fixture.descriptor.downloadSizeBytes;
    return fixture;
}

/// A context lending a session, installed for a case whose download selects its chunks in the engine.
struct InstalledSession
{
    InstalledSession() : context(an::tests::astrometry::openViewerContext())
    {
        if (context)
        {
            installed.emplace(*context);
        }
    }

    std::unique_ptr<an::tests::astrometry::ViewerTestContext> context;
    std::optional<an::tests::astrometry::InstalledContext> installed;
};

bool waitForSignal(QSignalSpy& spy, const int timeoutMs = 5000)
{
    return spy.count() > 0 || spy.wait(timeoutMs);
}

bool waitForCondition(const std::function<bool()>& predicate, const int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return predicate();
}

an::ui::CatalogDownloadResult takeResult(const QSignalSpy& spy)
{
    EXPECT_GT(spy.count(), 0);
    return qvariant_cast<an::ui::CatalogDownloadResult>(spy.at(0).at(0));
}

QVector<an::ui::CatalogDownloadProgress> takeProgresses(const QSignalSpy& spy)
{
    QVector<an::ui::CatalogDownloadProgress> progresses;
    progresses.reserve(spy.count());
    for (const QList<QVariant>& values : spy)
    {
        progresses.push_back(qvariant_cast<an::ui::CatalogDownloadProgress>(values.at(0)));
    }
    return progresses;
}

} // namespace

TEST(CatalogDownloadService, InstallsChunkCatalogFromManifest)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const CatalogSourceFixture fixture = createCatalogSource(sourceDir);

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    QSignalSpy progressSpy(&service, &an::ui::CatalogDownloadService::downloadProgress);

    service.downloadCatalogPack(fixture.pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    ASSERT_TRUE(result.success);
    EXPECT_FALSE(result.cancelled);

    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    EXPECT_EQ(result.installDirectory, installPath);
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("catalog_meta.json"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("gaia_dr3_g14_chunk_00.ansp"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("gaia_dr3_g14_chunk_01.ansp"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));
    EXPECT_FALSE(QFileInfo::exists(QDir(installRoot.path()).filePath(fixture.pack.id + QStringLiteral(".installing"))));

    const QVector<an::ui::CatalogDownloadProgress> progresses = takeProgresses(progressSpy);
    bool sawManifest = false;
    bool sawDownload = false;
    bool sawVerify = false;
    for (const auto& progress : progresses)
    {
        sawManifest = sawManifest || progress.phase == QStringLiteral("manifest");
        sawDownload = sawDownload || progress.phase == QStringLiteral("download");
        sawVerify = sawVerify || progress.phase == QStringLiteral("verify");
    }
    EXPECT_TRUE(sawManifest);
    EXPECT_TRUE(sawDownload);
    EXPECT_TRUE(sawVerify);
}

TEST(CatalogDownloadService, InstallsSpccAnxpCatalogFromCatalogMetaManifest)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const SpccCatalogSourceFixture fixture = createSpccCatalogSource(sourceDir);

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    QSignalSpy progressSpy(&service, &an::ui::CatalogDownloadService::downloadProgress);

    service.downloadCatalogPack(fixture.pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_FALSE(result.cancelled);

    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    EXPECT_EQ(result.installDirectory, installPath);
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("catalog_meta.json"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("chunk_manifest.json"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("coverage_summary.json"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("source_id_index.json"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("source_count_transcript.json"))));
    EXPECT_TRUE(QFileInfo::exists(
        QDir(installPath).filePath(QStringLiteral("chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));

    const QVector<an::ui::CatalogDownloadProgress> progresses = takeProgresses(progressSpy);
    bool sawSidecarVerify = false;
    bool sawChunkDownload = false;
    for (const auto& progress : progresses)
    {
        sawSidecarVerify = sawSidecarVerify || progress.currentFile == QStringLiteral("chunk_manifest.json");
        sawChunkDownload = sawChunkDownload ||
                           progress.currentFile == QStringLiteral("chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp");
    }
    EXPECT_TRUE(sawSidecarVerify);
    EXPECT_TRUE(sawChunkDownload);
}

TEST(CatalogDownloadService, RejectsSpccCatalogMetaChecksumMismatch)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    SpccCatalogSourceFixture fixture = createSpccCatalogSource(sourceDir);
    fixture.pack.catalogMetaSha256 = QString(64, QLatin1Char('0'));

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadCatalogPack(fixture.pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("checksum"), Qt::CaseInsensitive))
        << result.errorMessage.toStdString();

    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    EXPECT_FALSE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));
}

TEST(CatalogDownloadService, RejectsUnsafeSpccCatalogPackIdBeforeCreatingInstallPath)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    SpccCatalogSourceFixture fixture = createSpccCatalogSource(sourceDir);
    fixture.pack.id = QStringLiteral("../escaped-spcc-pack");

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadCatalogPack(fixture.pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("unsafe"), Qt::CaseInsensitive))
        << result.errorMessage.toStdString();
    EXPECT_TRUE(QDir(installRoot.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
}

TEST(CatalogDownloadService, InstallsSpccResponseRepositoryFromResponseMetaManifest)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const SpccResponseRepositorySourceFixture fixture = createSpccResponseRepositorySource(sourceDir);

    an::ui::CatalogDownloadService service;
    service.setSpccResponseRepositoryRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    QSignalSpy progressSpy(&service, &an::ui::CatalogDownloadService::downloadProgress);

    service.downloadSpccResponseRepository(fixture.descriptor);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_FALSE(result.cancelled);

    const QString installPath =
        QDir(QDir(installRoot.path()).filePath(fixture.descriptor.repositoryId)).filePath(fixture.descriptor.version);
    EXPECT_EQ(result.installDirectory, installPath);
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("response_meta.json"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("components/sensor_synthetic_r.json"))));
    EXPECT_TRUE(
        QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("white_references/white_synthetic_d65.json"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));
    EXPECT_FALSE(QFileInfo::exists(QDir(QDir(installRoot.path()).filePath(fixture.descriptor.repositoryId))
                                       .filePath(fixture.descriptor.version + QStringLiteral(".installing"))));

    const an::ui::SpccResponseRepositoryInspection inspection =
        an::ui::inspectSpccResponseRepositoryDetailed(installPath);
    EXPECT_EQ(inspection.status, an::ui::SpccResponseRepositoryStatus::Ready);
    EXPECT_EQ(inspection.repositoryId, fixture.descriptor.repositoryId);
    EXPECT_EQ(inspection.rightsReviewStatus, fixture.descriptor.rightsReviewStatus);

    const QVector<an::ui::CatalogDownloadProgress> progresses = takeProgresses(progressSpy);
    bool sawManifest = false;
    bool sawResponseAsset = false;
    for (const auto& progress : progresses)
    {
        sawManifest = sawManifest || progress.phase == QStringLiteral("manifest");
        sawResponseAsset =
            sawResponseAsset || progress.currentFile == QStringLiteral("components/sensor_synthetic_r.json");
    }
    EXPECT_TRUE(sawManifest);
    EXPECT_TRUE(sawResponseAsset);
}

TEST(CatalogDownloadService, InstallsSpccResponseRepositoryOverHttpHostedManifest)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    SpccResponseRepositorySourceFixture fixture = createSpccResponseRepositorySource(sourceDir);
    LocalHttpFileServer server(sourceDir.path());
    fixture.descriptor.manifestUrl = server.urlFor(QStringLiteral("response_meta.json"));

    an::ui::CatalogDownloadService service;
    service.setSpccResponseRepositoryRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadSpccResponseRepository(fixture.descriptor);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();

    const QString installPath =
        QDir(QDir(installRoot.path()).filePath(fixture.descriptor.repositoryId)).filePath(fixture.descriptor.version);
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("response_meta.json"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("components/sensor_synthetic_r.json"))));

    const QStringList requests = server.requestedPaths();
    EXPECT_TRUE(requests.contains(QStringLiteral("/response_meta.json")));
    EXPECT_TRUE(requests.contains(QStringLiteral("/components/sensor_synthetic_r.json")));
    EXPECT_TRUE(requests.contains(QStringLiteral("/white_references/white_synthetic_d65.json")));
}

TEST(CatalogDownloadService, FailedSpccResponseChecksumVerificationLeavesExistingInstallationUntouched)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const SpccResponseRepositorySourceFixture fixture = createSpccResponseRepositorySource(sourceDir, true);
    const QString installPath =
        QDir(QDir(installRoot.path()).filePath(fixture.descriptor.repositoryId)).filePath(fixture.descriptor.version);
    ASSERT_TRUE(QDir().mkpath(installPath));
    ASSERT_TRUE(writeBytes(QDir(installPath).filePath(QStringLiteral(".install_complete")), QByteArray("ok\n")));
    ASSERT_TRUE(
        writeBytes(QDir(installPath).filePath(QStringLiteral("keep.txt")), QByteArray("kept-response-install")));

    an::ui::CatalogDownloadService service;
    service.setSpccResponseRepositoryRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadSpccResponseRepository(fixture.descriptor);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("checksum"), Qt::CaseInsensitive))
        << result.errorMessage.toStdString();

    QFile keepFile(QDir(installPath).filePath(QStringLiteral("keep.txt")));
    ASSERT_TRUE(keepFile.open(QIODevice::ReadOnly));
    EXPECT_EQ(keepFile.readAll(), QByteArray("kept-response-install"));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));
}

TEST(CatalogDownloadService, RejectsSpccResponseRepositoryWhenAssetReviewIsNotApproved)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const SpccResponseRepositorySourceFixture fixture =
        createSpccResponseRepositorySource(sourceDir, false, QStringLiteral("private-local"));

    an::ui::CatalogDownloadService service;
    service.setSpccResponseRepositoryRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadSpccResponseRepository(fixture.descriptor);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("review status"), Qt::CaseInsensitive))
        << result.errorMessage.toStdString();

    const QString installPath =
        QDir(QDir(installRoot.path()).filePath(fixture.descriptor.repositoryId)).filePath(fixture.descriptor.version);
    EXPECT_FALSE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));
}

TEST(CatalogDownloadService, ReinstallingSpccResponseRepositoryReplacesExistingInstallAndCleansBackup)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const SpccResponseRepositorySourceFixture fixture = createSpccResponseRepositorySource(sourceDir);
    const QString installPath =
        QDir(QDir(installRoot.path()).filePath(fixture.descriptor.repositoryId)).filePath(fixture.descriptor.version);
    ASSERT_TRUE(QDir().mkpath(installPath));
    ASSERT_TRUE(writeBytes(QDir(installPath).filePath(QStringLiteral(".install_complete")), QByteArray("ok\n")));
    ASSERT_TRUE(writeBytes(QDir(installPath).filePath(QStringLiteral("old.txt")), QByteArray("old-response-install")));

    an::ui::CatalogDownloadService service;
    service.setSpccResponseRepositoryRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadSpccResponseRepository(fixture.descriptor);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();

    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("response_meta.json"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("components/sensor_synthetic_r.json"))));
    EXPECT_FALSE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("old.txt"))));
    EXPECT_FALSE(QFileInfo::exists(QDir(QDir(installRoot.path()).filePath(fixture.descriptor.repositoryId))
                                       .filePath(fixture.descriptor.version + QStringLiteral(".backup"))));
}

TEST(CatalogDownloadService, CancellingSpccResponseRepositoryDownloadLeavesNoCompletedInstall)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const SpccResponseRepositorySourceFixture fixture = createSpccResponseRepositorySource(sourceDir);

    an::ui::CatalogDownloadService service;
    service.setSpccResponseRepositoryRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);

    service.downloadSpccResponseRepository(fixture.descriptor);
    service.cancel();

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.cancelled);

    const QString installPath =
        QDir(QDir(installRoot.path()).filePath(fixture.descriptor.repositoryId)).filePath(fixture.descriptor.version);
    EXPECT_FALSE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));
}

TEST(CatalogDownloadService, InstallsSpccAnxpCatalogOverHttpHostedManifest)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    SpccCatalogSourceFixture fixture = createSpccCatalogSource(sourceDir);
    LocalHttpFileServer server(sourceDir.path());
    fixture.pack.manifestUrl = server.urlFor(QStringLiteral("catalog_meta.json"));

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    QSignalSpy progressSpy(&service, &an::ui::CatalogDownloadService::downloadProgress);

    service.downloadCatalogPack(fixture.pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_FALSE(result.cancelled);

    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("catalog_meta.json"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("chunk_manifest.json"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("coverage_summary.json"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("source_id_index.json"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("source_count_transcript.json"))));
    EXPECT_TRUE(QFileInfo::exists(
        QDir(installPath).filePath(QStringLiteral("chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));

    const QStringList requestedPaths = server.requestedPaths();
    EXPECT_TRUE(requestedPaths.contains(QStringLiteral("/catalog_meta.json")));
    EXPECT_TRUE(requestedPaths.contains(QStringLiteral("/chunk_manifest.json")));
    EXPECT_TRUE(requestedPaths.contains(QStringLiteral("/coverage_summary.json")));
    EXPECT_TRUE(requestedPaths.contains(QStringLiteral("/source_id_index.json")));
    EXPECT_TRUE(requestedPaths.contains(QStringLiteral("/source_count_transcript.json")));
    EXPECT_TRUE(requestedPaths.contains(QStringLiteral("/chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp")));

    const QVector<an::ui::CatalogDownloadProgress> progresses = takeProgresses(progressSpy);
    bool sawHttpChunkDownload = false;
    for (const auto& progress : progresses)
    {
        sawHttpChunkDownload =
            sawHttpChunkDownload ||
            progress.currentFile == QStringLiteral("chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp");
    }
    EXPECT_TRUE(sawHttpChunkDownload);
}

TEST(CatalogDownloadService, DownloadsOnlyRequiredSpccRegionChunks)
{
    const InstalledSession session;
    ASSERT_NE(session.context, nullptr);
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    SpccCatalogSourceFixture fixture = createSpccCatalogSource(sourceDir);
    addSecondSpccChunkForM31(sourceDir, fixture);
    LocalHttpFileServer server(sourceDir.path());
    fixture.pack.manifestUrl = server.urlFor(QStringLiteral("catalog_meta.json"));

    an::ui::CatalogDownloadService service;
    an::ui::SpccCatalogChunkDownloadRequest request{
        .pack = fixture.pack,
        .installRootDirectory = installRoot.path(),
        .mode = an::ui::SpccCatalogChunkDownloadMode::RequiredRegion,
        .hasRegion = true,
        .centerRaDeg = 10.6847,
        .centerDecDeg = 41.2692,
        .radiusDeg = 0.05,
    };

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadSpccCatalogChunks(request);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();

    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    EXPECT_TRUE(QFileInfo::exists(
        QDir(installPath).filePath(QStringLiteral("chunks/spcc_gaia_dr3_xp_sampled_chunk_0001.anxp"))));
    EXPECT_FALSE(QFileInfo::exists(
        QDir(installPath).filePath(QStringLiteral("chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp"))));
    EXPECT_FALSE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));

    const QStringList requestedPaths = server.requestedPaths();
    EXPECT_TRUE(requestedPaths.contains(QStringLiteral("/chunks/spcc_gaia_dr3_xp_sampled_chunk_0001.anxp")));
    EXPECT_FALSE(requestedPaths.contains(QStringLiteral("/chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp")));
}

TEST(CatalogDownloadService, RehydratesSpccChunkRequestManifestUrlFromManagedDescriptor)
{
    const InstalledSession session;
    ASSERT_NE(session.context, nullptr);
    const StandardPathsTestMode testMode;

    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    SpccCatalogSourceFixture fixture = createSpccCatalogSource(sourceDir);
    addSecondSpccChunkForM31(sourceDir, fixture);
    LocalHttpFileServer server(sourceDir.path());

    const QString configPath = an::ui::spccCatalogMirrorConfigFilePath();
    ASSERT_TRUE(QDir().mkpath(QFileInfo(configPath).dir().path()));
    QFile mirrorConfig(configPath);
    ASSERT_TRUE(mirrorConfig.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QJsonObject descriptor{
        {QStringLiteral("id"), fixture.pack.id},
        {QStringLiteral("display_name"), fixture.pack.displayName},
        {QStringLiteral("description"), QStringLiteral("Managed SPCC test descriptor")},
        {QStringLiteral("manifest_url"), server.urlFor(QStringLiteral("catalog_meta.json")).toString()},
        {QStringLiteral("catalog_meta_sha256"), fixture.pack.catalogMetaSha256},
        {QStringLiteral("distribution_type"), QStringLiteral("chunks")},
        {QStringLiteral("catalog_family_id"), QStringLiteral("spcc_gaia_dr3_xp_sampled")},
        {QStringLiteral("access_profile"), QStringLiteral("installed-regional-cache")},
        {QStringLiteral("rights_review_status"), QStringLiteral("approved-for-managed-download")},
        {QStringLiteral("recommended"), true},
        {QStringLiteral("download_size_bytes"), static_cast<double>(fixture.pack.downloadSizeBytes)},
        {QStringLiteral("installed_size_bytes"), static_cast<double>(fixture.pack.installedSizeBytes)},
        {QStringLiteral("coverage"),
         QJsonObject{
             {QStringLiteral("center_ra_deg"), 10.6847},
             {QStringLiteral("center_dec_deg"), 41.2692},
             {QStringLiteral("radius_deg"), 1.0},
             {QStringLiteral("magnitude_limit"), 17.0},
         }},
    };
    const QJsonObject config{
        {QStringLiteral("schema_version"), QStringLiteral("spcc-catalog-mirror-config-v1")},
        {QStringLiteral("mirrors"), QJsonArray{server.urlFor(QString()).toString()}},
        {QStringLiteral("catalog_packs"), QJsonArray{descriptor}},
    };
    ASSERT_GT(mirrorConfig.write(QJsonDocument(config).toJson(QJsonDocument::Compact)), 0);
    mirrorConfig.close();

    an::ui::CatalogPackDescriptor stalePack = fixture.pack;
    stalePack.manifestUrl = QUrl();

    an::ui::CatalogDownloadService service;
    an::ui::SpccCatalogChunkDownloadRequest request{
        .pack = stalePack,
        .installRootDirectory = installRoot.path(),
        .mode = an::ui::SpccCatalogChunkDownloadMode::RequiredRegion,
        .hasRegion = true,
        .centerRaDeg = 10.6847,
        .centerDecDeg = 41.2692,
        .radiusDeg = 0.05,
    };

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadSpccCatalogChunks(request);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();

    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    EXPECT_TRUE(QFileInfo::exists(
        QDir(installPath).filePath(QStringLiteral("chunks/spcc_gaia_dr3_xp_sampled_chunk_0001.anxp"))));
    EXPECT_TRUE(server.requestedPaths().contains(QStringLiteral("/catalog_meta.json")));
    EXPECT_TRUE(server.requestedPaths().contains(QStringLiteral("/chunks/spcc_gaia_dr3_xp_sampled_chunk_0001.anxp")));
}

TEST(CatalogDownloadService, ReusesVerifiedRequiredSpccChunkAlreadyCached)
{
    const InstalledSession session;
    ASSERT_NE(session.context, nullptr);
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    SpccCatalogSourceFixture fixture = createSpccCatalogSource(sourceDir);
    const QByteArray m31ChunkBytes = addSecondSpccChunkForM31(sourceDir, fixture);
    LocalHttpFileServer server(sourceDir.path());
    fixture.pack.manifestUrl = server.urlFor(QStringLiteral("catalog_meta.json"));

    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    ASSERT_TRUE(writeBytes(
        QDir(installPath).filePath(QStringLiteral("chunks/spcc_gaia_dr3_xp_sampled_chunk_0001.anxp")), m31ChunkBytes));
    ASSERT_TRUE(QFile::remove(
        QDir(sourceDir.path()).filePath(QStringLiteral("chunks/spcc_gaia_dr3_xp_sampled_chunk_0001.anxp"))));

    an::ui::CatalogDownloadService service;
    an::ui::SpccCatalogChunkDownloadRequest request{
        .pack = fixture.pack,
        .installRootDirectory = installRoot.path(),
        .mode = an::ui::SpccCatalogChunkDownloadMode::RequiredRegion,
        .hasRegion = true,
        .centerRaDeg = 10.6847,
        .centerDecDeg = 41.2692,
        .radiusDeg = 0.05,
    };

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadSpccCatalogChunks(request);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();

    EXPECT_FALSE(server.requestedPaths().contains(QStringLiteral("/chunks/spcc_gaia_dr3_xp_sampled_chunk_0001.anxp")));
}

TEST(CatalogDownloadService, ARegionRequestWithoutASessionSaysSo)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    SpccCatalogSourceFixture fixture = createSpccCatalogSource(sourceDir);
    LocalHttpFileServer server(sourceDir.path());
    fixture.pack.manifestUrl = server.urlFor(QStringLiteral("catalog_meta.json"));

    an::ui::CatalogDownloadService service;
    an::ui::SpccCatalogChunkDownloadRequest request{
        .pack = fixture.pack,
        .installRootDirectory = installRoot.path(),
        .mode = an::ui::SpccCatalogChunkDownloadMode::RequiredRegion,
        .hasRegion = true,
        .centerRaDeg = 10.6847,
        .centerDecDeg = 41.2692,
        .radiusDeg = 0.05,
    };

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadSpccCatalogChunks(request);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("No engine session"))) << result.errorMessage.toStdString();
    EXPECT_FALSE(server.requestedPaths().contains(QStringLiteral("/chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp")));
}

TEST(CatalogDownloadService, ARegionNoChunkCoversIsRefusedBeforeAnyChunkIsFetched)
{
    const InstalledSession session;
    ASSERT_NE(session.context, nullptr);
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    SpccCatalogSourceFixture fixture = createSpccCatalogSource(sourceDir);
    LocalHttpFileServer server(sourceDir.path());
    fixture.pack.manifestUrl = server.urlFor(QStringLiteral("catalog_meta.json"));

    an::ui::CatalogDownloadService service;
    an::ui::SpccCatalogChunkDownloadRequest request{
        .pack = fixture.pack,
        .installRootDirectory = installRoot.path(),
        .mode = an::ui::SpccCatalogChunkDownloadMode::RequiredRegion,
        .hasRegion = true,
        .centerRaDeg = 200.0,
        .centerDecDeg = -60.0,
        .radiusDeg = 0.05,
    };

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadSpccCatalogChunks(request);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    for (const QString& path : server.requestedPaths())
    {
        EXPECT_FALSE(path.startsWith(QStringLiteral("/chunks/"))) << path.toStdString();
    }
}

TEST(CatalogDownloadService, DownloadsCatalogChunksConcurrentlyOverHttp)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    CatalogSourceFixture fixture = createCatalogSource(sourceDir);
    LocalHttpFileServer server(
        sourceDir.path(),
        {QStringLiteral("/gaia_dr3_g14_chunk_00.ansp"), QStringLiteral("/gaia_dr3_g14_chunk_01.ansp")}, 200);
    fixture.pack.manifestUrl = server.urlFor(QStringLiteral("catalog_meta.json"));

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    QSignalSpy progressSpy(&service, &an::ui::CatalogDownloadService::downloadProgress);
    service.downloadCatalogPack(fixture.pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();
    EXPECT_GE(server.maxInFlightRequests(), 2);

    qint64 lastAggregateBytes = -1;
    for (const an::ui::CatalogDownloadProgress& progress : takeProgresses(progressSpy))
    {
        if (progress.bytesTotal <= 0 || progress.filesTotal <= 0 ||
            (progress.phase != QStringLiteral("download") && progress.phase != QStringLiteral("verify")))
        {
            continue;
        }

        EXPECT_GE(progress.bytesReceived, lastAggregateBytes)
            << progress.phase.toStdString() << " " << progress.currentFile.toStdString();
        lastAggregateBytes = progress.bytesReceived;
    }
}

TEST(CatalogDownloadService, CancellingConcurrentGaiaDownloadCleansStagingAndEmitsOnce)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    CatalogSourceFixture fixture = createCatalogSource(sourceDir);
    LocalHttpFileServer server(
        sourceDir.path(),
        {QStringLiteral("/gaia_dr3_g14_chunk_00.ansp"), QStringLiteral("/gaia_dr3_g14_chunk_01.ansp")}, 500);
    fixture.pack.manifestUrl = server.urlFor(QStringLiteral("catalog_meta.json"));

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    QSignalSpy progressSpy(&service, &an::ui::CatalogDownloadService::downloadProgress);

    service.downloadCatalogPack(fixture.pack);
    ASSERT_TRUE(waitForCondition(
        [&progressSpy] {
            for (const an::ui::CatalogDownloadProgress& progress : takeProgresses(progressSpy))
            {
                if (progress.phase == QStringLiteral("download"))
                {
                    return true;
                }
            }
            return false;
        },
        2000));

    service.cancel();

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.cancelled);
    EXPECT_EQ(finishedSpy.count(), 1);

    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    const QString stagingPath = QDir(installRoot.path()).filePath(fixture.pack.id + QStringLiteral(".installing"));
    EXPECT_FALSE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));
    EXPECT_FALSE(QFileInfo::exists(stagingPath));

    EXPECT_FALSE(finishedSpy.wait(100));
    EXPECT_EQ(finishedSpy.count(), 1);
}

TEST(CatalogDownloadService, InstallsOptionalSpccRegionalRequestSidecar)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    SpccCatalogSourceFixture fixture =
        createSpccCatalogSource(sourceDir, QStringLiteral("installed-regional-cache"),
                                QStringLiteral("approved-for-managed-download"), false, true);
    LocalHttpFileServer server(sourceDir.path());
    fixture.pack.manifestUrl = server.urlFor(QStringLiteral("catalog_meta.json"));

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadCatalogPack(fixture.pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();

    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("regional_cache_request.json"))));
    EXPECT_TRUE(server.requestedPaths().contains(QStringLiteral("/regional_cache_request.json")));
}

TEST(CatalogDownloadService, RejectsSpccCatalogWhenHostedManifestRightsDoNotMatchDescriptor)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const SpccCatalogSourceFixture fixture =
        createSpccCatalogSource(sourceDir, QStringLiteral("installed-regional-cache"), QStringLiteral("private-local"));

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadCatalogPack(fixture.pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("rights"), Qt::CaseInsensitive))
        << result.errorMessage.toStdString();

    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    EXPECT_FALSE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));
}

TEST(CatalogDownloadService, ReusesVerifiedSpccSidecarAndChunkAlreadyCached)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const SpccCatalogSourceFixture fixture = createSpccCatalogSource(sourceDir);
    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    ASSERT_TRUE(QDir().mkpath(installPath));
    ASSERT_TRUE(
        writeBytes(QDir(installPath).filePath(QStringLiteral("chunk_manifest.json")), fixture.chunkManifestBytes));
    ASSERT_TRUE(
        writeBytes(QDir(installPath).filePath(QStringLiteral("chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp")),
                   fixture.chunkBytes));

    ASSERT_TRUE(QFile::remove(fixture.chunkManifestPath));
    ASSERT_TRUE(QFile::remove(fixture.chunkPath));

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadCatalogPack(fixture.pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();

    QFile reusedSidecar(QDir(installPath).filePath(QStringLiteral("chunk_manifest.json")));
    QFile reusedChunk(QDir(installPath).filePath(QStringLiteral("chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp")));
    ASSERT_TRUE(reusedSidecar.open(QIODevice::ReadOnly));
    ASSERT_TRUE(reusedChunk.open(QIODevice::ReadOnly));
    EXPECT_EQ(reusedSidecar.readAll(), fixture.chunkManifestBytes);
    EXPECT_EQ(reusedChunk.readAll(), fixture.chunkBytes);
}

TEST(CatalogDownloadService, FailedSpccChecksumVerificationLeavesExistingInstallationUntouched)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const SpccCatalogSourceFixture fixture = createSpccCatalogSource(
        sourceDir, QStringLiteral("installed-regional-cache"), QStringLiteral("approved-for-managed-download"), true);

    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    ASSERT_TRUE(QDir().mkpath(installPath));
    ASSERT_TRUE(writeBytes(QDir(installPath).filePath(QStringLiteral(".install_complete")), QByteArray("ok\n")));
    ASSERT_TRUE(writeBytes(QDir(installPath).filePath(QStringLiteral("keep.txt")), QByteArray("kept-spcc-install")));

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadCatalogPack(fixture.pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_TRUE(result.errorMessage.contains(QStringLiteral("checksum"), Qt::CaseInsensitive))
        << result.errorMessage.toStdString();

    QFile keepFile(QDir(installPath).filePath(QStringLiteral("keep.txt")));
    ASSERT_TRUE(keepFile.open(QIODevice::ReadOnly));
    EXPECT_EQ(keepFile.readAll(), QByteArray("kept-spcc-install"));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));
}

TEST(CatalogDownloadService, FullSpccCatalogDownloadPreservesUnrelatedCachedFiles)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const SpccCatalogSourceFixture fixture = createSpccCatalogSource(sourceDir);
    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    ASSERT_TRUE(QDir().mkpath(installPath));
    ASSERT_TRUE(writeBytes(QDir(installPath).filePath(QStringLiteral(".install_complete")), QByteArray("ok\n")));
    ASSERT_TRUE(writeBytes(QDir(installPath).filePath(QStringLiteral("old.txt")), QByteArray("old-spcc-install")));

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadCatalogPack(fixture.pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();

    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("catalog_meta.json"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("chunk_manifest.json"))));
    EXPECT_TRUE(QFileInfo::exists(
        QDir(installPath).filePath(QStringLiteral("chunks/spcc_gaia_dr3_xp_sampled_chunk_0000.anxp"))));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral("old.txt"))));
    EXPECT_FALSE(QFileInfo::exists(QDir(installRoot.path()).filePath(fixture.pack.id + QStringLiteral(".backup"))));
}

TEST(CatalogDownloadService, CancellingSpccDownloadLeavesNoCompletedInstall)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const SpccCatalogSourceFixture fixture = createSpccCatalogSource(sourceDir);

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);

    service.downloadCatalogPack(fixture.pack);
    service.cancel();

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.cancelled);

    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    EXPECT_FALSE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));
}

TEST(CatalogDownloadService, ReusesVerifiedChunkAlreadyInStagingDirectory)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const CatalogSourceFixture fixture = createCatalogSource(sourceDir);
    const QString stagingDir = QDir(installRoot.path()).filePath(fixture.pack.id + QStringLiteral(".installing"));
    ASSERT_TRUE(QDir().mkpath(stagingDir));
    ASSERT_TRUE(
        writeBytes(QDir(stagingDir).filePath(QStringLiteral("gaia_dr3_g14_chunk_00.ansp")), fixture.chunk0Bytes));

    ASSERT_TRUE(QFile::remove(fixture.chunk0Path));

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadCatalogPack(fixture.pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();

    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    QFile reusedChunk(QDir(installPath).filePath(QStringLiteral("gaia_dr3_g14_chunk_00.ansp")));
    ASSERT_TRUE(reusedChunk.open(QIODevice::ReadOnly));
    EXPECT_EQ(reusedChunk.readAll(), fixture.chunk0Bytes);
}

TEST(CatalogDownloadService, FailedVerificationLeavesExistingInstallationUntouched)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const CatalogSourceFixture fixture = createCatalogSource(sourceDir, true);

    const QString installPath = QDir(installRoot.path()).filePath(fixture.pack.id);
    ASSERT_TRUE(QDir().mkpath(installPath));
    ASSERT_TRUE(writeBytes(QDir(installPath).filePath(QStringLiteral(".install_complete")), QByteArray("ok\n")));
    ASSERT_TRUE(writeBytes(QDir(installPath).filePath(QStringLiteral("keep.txt")), QByteArray("kept-install")));

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadCatalogPack(fixture.pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_FALSE(result.errorMessage.isEmpty());

    QFile keepFile(QDir(installPath).filePath(QStringLiteral("keep.txt")));
    ASSERT_TRUE(keepFile.open(QIODevice::ReadOnly));
    EXPECT_EQ(keepFile.readAll(), QByteArray("kept-install"));
    EXPECT_TRUE(QFileInfo::exists(QDir(installPath).filePath(QStringLiteral(".install_complete"))));
}

TEST(CatalogDownloadService, CancellationEmitsCancelledResult)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const CatalogSourceFixture fixture = createCatalogSource(sourceDir);

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);

    service.downloadCatalogPack(fixture.pack);
    service.cancel();

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.cancelled);
}

TEST(CatalogDownloadService, MissingManifestEmitsFailureResult)
{
    QTemporaryDir installRoot;
    ASSERT_TRUE(installRoot.isValid());

    an::ui::CatalogPackDescriptor pack;
    pack.id = QStringLiteral("gaia_dr3_g14");
    pack.manifestUrl = QUrl::fromLocalFile(QStringLiteral("/nonexistent/path/catalog_meta.json"));
    pack.distributionType = QStringLiteral("chunks");

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadCatalogPack(pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_FALSE(result.errorMessage.isEmpty());
}

TEST(CatalogDownloadService, ManifestWithNonContiguousIndicesIsRejected)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const QString manifestPath = QDir(sourceDir.path()).filePath(QStringLiteral("catalog_meta.json"));
    QJsonObject root;
    root.insert(QStringLiteral("format_version"), 3);
    root.insert(QStringLiteral("storage"), QStringLiteral("chunks"));
    root.insert(QStringLiteral("catalog_id"), QStringLiteral("gaia_dr3_g14"));
    root.insert(QStringLiteral("chunk_count"), 2);
    root.insert(QStringLiteral("chunks"), QJsonArray{
                                              QJsonObject{
                                                  {QStringLiteral("index"), 0},
                                                  {QStringLiteral("file"), QStringLiteral("chunk_00.ansp")},
                                                  {QStringLiteral("size_bytes"), 16.0},
                                                  {QStringLiteral("sha256"), QString(64, QLatin1Char('0'))},
                                              },
                                              QJsonObject{
                                                  {QStringLiteral("index"), 2}, // gap: index 1 is missing
                                                  {QStringLiteral("file"), QStringLiteral("chunk_02.ansp")},
                                                  {QStringLiteral("size_bytes"), 16.0},
                                                  {QStringLiteral("sha256"), QString(64, QLatin1Char('0'))},
                                              },
                                          });
    ASSERT_TRUE(writeBytes(manifestPath, QJsonDocument(root).toJson(QJsonDocument::Compact)));

    an::ui::CatalogPackDescriptor pack;
    pack.id = QStringLiteral("gaia_dr3_g14");
    pack.manifestUrl = QUrl::fromLocalFile(manifestPath);
    pack.distributionType = QStringLiteral("chunks");

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadCatalogPack(pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_FALSE(result.errorMessage.isEmpty());
}

TEST(CatalogDownloadService, ManifestWithDuplicateChunkFilesIsRejected)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const QString manifestPath = QDir(sourceDir.path()).filePath(QStringLiteral("catalog_meta.json"));
    QJsonObject root;
    root.insert(QStringLiteral("format_version"), 3);
    root.insert(QStringLiteral("storage"), QStringLiteral("chunks"));
    root.insert(QStringLiteral("catalog_id"), QStringLiteral("gaia_dr3_g14"));
    root.insert(QStringLiteral("chunk_count"), 2);
    root.insert(QStringLiteral("chunks"),
                QJsonArray{
                    QJsonObject{
                        {QStringLiteral("index"), 0},
                        {QStringLiteral("file"), QStringLiteral("chunk_00.ansp")},
                        {QStringLiteral("size_bytes"), 16.0},
                        {QStringLiteral("sha256"), QString(64, QLatin1Char('0'))},
                    },
                    QJsonObject{
                        {QStringLiteral("index"), 1},
                        {QStringLiteral("file"), QStringLiteral("chunk_00.ansp")}, // duplicate
                        {QStringLiteral("size_bytes"), 16.0},
                        {QStringLiteral("sha256"), QString(64, QLatin1Char('0'))},
                    },
                });
    ASSERT_TRUE(writeBytes(manifestPath, QJsonDocument(root).toJson(QJsonDocument::Compact)));

    an::ui::CatalogPackDescriptor pack;
    pack.id = QStringLiteral("gaia_dr3_g14");
    pack.manifestUrl = QUrl::fromLocalFile(manifestPath);
    pack.distributionType = QStringLiteral("chunks");

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadCatalogPack(pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_FALSE(result.errorMessage.isEmpty());
}

TEST(CatalogDownloadService, ManifestWithAbsoluteChunkUrlIsRejected)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const QString manifestPath = QDir(sourceDir.path()).filePath(QStringLiteral("catalog_meta.json"));
    QJsonObject root;
    root.insert(QStringLiteral("format_version"), 3);
    root.insert(QStringLiteral("storage"), QStringLiteral("chunks"));
    root.insert(QStringLiteral("catalog_id"), QStringLiteral("gaia_dr3_g14"));
    root.insert(QStringLiteral("chunk_count"), 1);
    root.insert(QStringLiteral("chunks"),
                QJsonArray{
                    QJsonObject{
                        {QStringLiteral("index"), 0},
                        {QStringLiteral("file"), QStringLiteral("https://example.com/chunk_00.ansp")},
                        {QStringLiteral("size_bytes"), 16.0},
                        {QStringLiteral("sha256"), QString(64, QLatin1Char('0'))},
                    },
                });
    ASSERT_TRUE(writeBytes(manifestPath, QJsonDocument(root).toJson(QJsonDocument::Compact)));

    an::ui::CatalogPackDescriptor pack;
    pack.id = QStringLiteral("gaia_dr3_g14");
    pack.manifestUrl = QUrl::fromLocalFile(manifestPath);
    pack.distributionType = QStringLiteral("chunks");

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadCatalogPack(pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_FALSE(result.errorMessage.isEmpty());
}

TEST(CatalogDownloadService, ManifestWithInvalidSha256IsRejected)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const QString manifestPath = QDir(sourceDir.path()).filePath(QStringLiteral("catalog_meta.json"));
    QJsonObject root;
    root.insert(QStringLiteral("format_version"), 3);
    root.insert(QStringLiteral("storage"), QStringLiteral("chunks"));
    root.insert(QStringLiteral("catalog_id"), QStringLiteral("gaia_dr3_g14"));
    root.insert(QStringLiteral("chunk_count"), 1);
    root.insert(QStringLiteral("chunks"), QJsonArray{
                                              QJsonObject{
                                                  {QStringLiteral("index"), 0},
                                                  {QStringLiteral("file"), QStringLiteral("chunk_00.ansp")},
                                                  {QStringLiteral("size_bytes"), 16.0},
                                                  {QStringLiteral("sha256"), QStringLiteral("not-a-valid-sha256")},
                                              },
                                          });
    ASSERT_TRUE(writeBytes(manifestPath, QJsonDocument(root).toJson(QJsonDocument::Compact)));

    an::ui::CatalogPackDescriptor pack;
    pack.id = QStringLiteral("gaia_dr3_g14");
    pack.manifestUrl = QUrl::fromLocalFile(manifestPath);
    pack.distributionType = QStringLiteral("chunks");

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadCatalogPack(pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_FALSE(result.errorMessage.isEmpty());
}

TEST(CatalogDownloadService, ManifestWithZeroChunkSizeIsRejected)
{
    QTemporaryDir sourceDir;
    QTemporaryDir installRoot;
    ASSERT_TRUE(sourceDir.isValid());
    ASSERT_TRUE(installRoot.isValid());

    const QString manifestPath = QDir(sourceDir.path()).filePath(QStringLiteral("catalog_meta.json"));
    QJsonObject root;
    root.insert(QStringLiteral("format_version"), 3);
    root.insert(QStringLiteral("storage"), QStringLiteral("chunks"));
    root.insert(QStringLiteral("catalog_id"), QStringLiteral("gaia_dr3_g14"));
    root.insert(QStringLiteral("chunk_count"), 1);
    root.insert(QStringLiteral("chunks"), QJsonArray{
                                              QJsonObject{
                                                  {QStringLiteral("index"), 0},
                                                  {QStringLiteral("file"), QStringLiteral("chunk_00.ansp")},
                                                  {QStringLiteral("size_bytes"), 0.0}, // invalid
                                                  {QStringLiteral("sha256"), QString(64, QLatin1Char('0'))},
                                              },
                                          });
    ASSERT_TRUE(writeBytes(manifestPath, QJsonDocument(root).toJson(QJsonDocument::Compact)));

    an::ui::CatalogPackDescriptor pack;
    pack.id = QStringLiteral("gaia_dr3_g14");
    pack.manifestUrl = QUrl::fromLocalFile(manifestPath);
    pack.distributionType = QStringLiteral("chunks");

    an::ui::CatalogDownloadService service;
    service.setCatalogRootDirectory(installRoot.path());

    QSignalSpy finishedSpy(&service, &an::ui::CatalogDownloadService::downloadFinished);
    service.downloadCatalogPack(pack);

    ASSERT_TRUE(waitForSignal(finishedSpy));
    const an::ui::CatalogDownloadResult result = takeResult(finishedSpy);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_FALSE(result.errorMessage.isEmpty());
}
