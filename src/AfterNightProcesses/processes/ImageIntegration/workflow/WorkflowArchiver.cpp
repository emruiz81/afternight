// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowArchiver.h"

#include "processes/ImageIntegration/ImageIntegrationConstants.h"
#include "processes/ImageIntegration/model/IntegrationError.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/RejectionMapUtils.h"
#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/ImageIntegration/reporting/ReportModel.h"
#include "processes/ImageIntegration/reporting/ReportModelBuilder.h"
#include "processes/ImageIntegration/reporting/ReportSources.h"
#include "processes/ImageIntegration/reporting/WorkflowReport.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/PreviewThumbnailGenerator.h"
#include "processes/ImageIntegration/workflow/WorkflowFileOperations.h"

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QTimeZone>
#include <QDirIterator>
#include <QCoreApplication>
#include <QBuffer>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPen>
#include <QSaveFile>
#include <QSet>
#include <QSqlQuery>
#include <QUuid>
#include <algorithm>
#include <chrono>
#include <limits>

namespace
{
[[nodiscard]] Qt::CaseSensitivity archivePathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

[[nodiscard]] bool isPathContainedBy(const QString& parent_path, const QString& candidate_path)
{
    QString parent = QDir::cleanPath(QFileInfo(parent_path).absoluteFilePath());
    const QString candidate = QDir::cleanPath(QFileInfo(candidate_path).absoluteFilePath());
    if (!parent.endsWith(QLatin1Char('/')))
    {
        parent.append(QLatin1Char('/'));
    }
    return candidate.startsWith(parent, archivePathCaseSensitivity());
}

[[nodiscard]] bool isSafeArchivePathComponent(const QString& value)
{
    return !value.isEmpty() && value == value.trimmed() && value != QStringLiteral(".") &&
           value != QStringLiteral("..") && !QDir::isAbsolutePath(value) && !value.contains(QLatin1Char('/')) &&
           !value.contains(QLatin1Char('\\')) && !value.contains(QLatin1Char(':'));
}

[[nodiscard]] qint64 saturatingAddBytes(const qint64 left, const qint64 right)
{
    constexpr qint64 kMaximum = (std::numeric_limits<qint64>::max)();
    if (left < 0 || right < 0 || left > kMaximum - right)
    {
        return kMaximum;
    }
    return left + right;
}

[[nodiscard]] qint64 directorySizeBytes(const QString& path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        total = saturatingAddBytes(total, (std::max)(static_cast<qint64>(0), it.fileInfo().size()));
    }
    return total;
}

/// The folder's files together: their sizes and how many there are.
struct DirectoryUse
{
    qint64 bytes = 0;
    int files = 0;
};

[[nodiscard]] DirectoryUse directoryUse(const QString& path)
{
    DirectoryUse use;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        use.bytes = saturatingAddBytes(use.bytes, (std::max)(static_cast<qint64>(0), it.fileInfo().size()));
        ++use.files;
    }
    return use;
}

[[nodiscard]] qint64 pendingMandatoryMetadataBytes(const WorkflowManifest& manifest,
                                                   const WorkflowExecutor::Result& result)
{
    // Account for unbounded runtime strings and quality data explicitly, before payload admission, so that what is
    // written afterwards cannot trigger copy-then-delete.
    constexpr qint64 kReportAndManifestEnvelope = 32 * 1024;
    constexpr qint64 kPerGroupEnvelope = 8 * 1024;
    constexpr qint64 kPerStageEnvelope = 2 * 1024;

    QJsonObject variableMetadata;
    QJsonArray warnings;
    for (const auto& warning : result.warnings)
    {
        warnings.append(warning);
    }
    variableMetadata[QStringLiteral("warnings")] = warnings;

    QJsonArray errors;
    for (const auto& error : result.errors)
    {
        errors.append(error);
    }
    variableMetadata[QStringLiteral("errors")] = errors;
    variableMetadata[QStringLiteral("errorMessage")] = result.errorMessage;

    QJsonArray groups;
    for (const auto& group : result.integrationGroupResults)
    {
        QJsonObject groupMetadata;
        groupMetadata[QStringLiteral("groupKey")] = group.groupKey;
        groupMetadata[QStringLiteral("outputPath")] = group.outputPath;
        groupMetadata[QStringLiteral("outputFilename")] = group.outputFilename;
        groupMetadata[QStringLiteral("outputRetentionReason")] = group.outputRetentionReason;
        groupMetadata[QStringLiteral("registrationGroupKey")] = group.registrationGroupKey;
        groupMetadata[QStringLiteral("registrationInputPolicy")] = group.registrationInputPolicy;
        groupMetadata[QStringLiteral("masterSolution")] = masterSolutionOutcomeToString(group.masterSolution);
        groupMetadata[QStringLiteral("masterSolutionDetail")] = group.masterSolutionDetail;
        if (group.qualityReport.valid)
        {
            groupMetadata[QStringLiteral("qualityReport")] = stackingQualityReportToJson(group.qualityReport);
        }
        if (!group.droppedLights.empty())
        {
            groupMetadata[QStringLiteral("droppedLights")] = droppedLightsToJson(group.droppedLights);
        }
        groups.append(groupMetadata);
    }
    variableMetadata[QStringLiteral("groups")] = groups;
    if (!result.reusedIntegrationGroups.empty())
    {
        variableMetadata[QStringLiteral("reusedIntegrationGroups")] =
            reusedIntegrationGroupsToJson(result.reusedIntegrationGroups);
    }

    QJsonArray stages;
    for (const auto& stage : result.stageResults)
    {
        QJsonObject stageMetadata;
        stageMetadata[QStringLiteral("name")] = stage.stageName;
        stageMetadata[QStringLiteral("status")] = stage.status;
        stages.append(stageMetadata);
    }
    variableMetadata[QStringLiteral("stages")] = stages;

    const QByteArray serializedManifest = QJsonDocument(manifest.toJson()).toJson(QJsonDocument::Indented);
    const QByteArray serializedVariableMetadata = QJsonDocument(variableMetadata).toJson(QJsonDocument::Indented);

    // What the run itself states, in the files that state it: the manifest and the log once each.
    qint64 stated = result.capturedLog.toUtf8().size();
    stated = saturatingAddBytes(stated, serializedManifest.size());
    stated = saturatingAddBytes(stated, serializedVariableMetadata.size());
    qint64 total = stated;
    if (ImageIntegration::runHasWorkflowReport(manifest, result))
    {
        // The report's model and its document each state the run's words once more - its log, its messages, its
        // files and its settings - and beyond them grow with the lights and the masters.
        total = saturatingAddBytes(total, saturatingAddBytes(stated, stated));
        qint64 lights = static_cast<qint64>(manifest.leftOutLights.size());
        for (const auto& group : manifest.integrationGroups)
        {
            lights += static_cast<qint64>(group.lightFiles.size());
        }
        const qint64 masters = static_cast<qint64>(
            (std::max)(manifest.integrationGroups.size(),
                       result.integrationGroupResults.size() + result.reusedIntegrationGroups.size()));
        total = saturatingAddBytes(total, ImageIntegration::Config::kReportArchiveBaseBytes);
        total = saturatingAddBytes(total, lights * ImageIntegration::Config::kReportArchiveBytesPerLight);
        total = saturatingAddBytes(total, masters * ImageIntegration::Config::kReportArchiveBytesPerMaster);
    }
    total = saturatingAddBytes(total, kReportAndManifestEnvelope);
    total = saturatingAddBytes(total, static_cast<qint64>(result.integrationGroupResults.size()) * kPerGroupEnvelope);
    total = saturatingAddBytes(total, static_cast<qint64>(result.stageResults.size()) * kPerStageEnvelope);
    return total;
}

[[nodiscard]] std::vector<QFileInfo> sortedHistoryRunDirectories(const QDir& historyDir,
                                                                 const QString& excludedRunPath = {})
{
    std::vector<QFileInfo> runDirs;
    const QString normalizedExcluded = QDir::cleanPath(excludedRunPath);
    const QFileInfoList entries = historyDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    runDirs.reserve(static_cast<size_t>(entries.size()));
    for (const auto& entry : entries)
    {
        if (!normalizedExcluded.isEmpty() && QDir::cleanPath(entry.absoluteFilePath()) == normalizedExcluded)
        {
            continue;
        }
        runDirs.push_back(entry);
    }

    std::sort(runDirs.begin(), runDirs.end(), [](const QFileInfo& left, const QFileInfo& right) {
        const qint64 leftTime = left.lastModified().toMSecsSinceEpoch();
        const qint64 rightTime = right.lastModified().toMSecsSinceEpoch();
        return leftTime == rightTime ? left.fileName() < right.fileName() : leftTime < rightTime;
    });
    return runDirs;
}

struct ArchiveRetentionPreflight
{
    bool retainPayloads = false;
    QString reason;
};

[[nodiscard]] ArchiveRetentionPreflight preflightArchiveRetention(const QDir& historyDir, const QString& currentRunPath,
                                                                  const qint64 knownCurrentRunBytes,
                                                                  const int maxHistoryEntries,
                                                                  const qint64 maxHistoryBytes)
{
    if (knownCurrentRunBytes > maxHistoryBytes)
    {
        return {false,
                QStringLiteral("known archive data (%1 bytes) exceeds the configured history byte limit (%2 bytes)")
                    .arg(knownCurrentRunBytes)
                    .arg(maxHistoryBytes)};
    }

    std::vector<QFileInfo> oldRuns = sortedHistoryRunDirectories(historyDir, currentRunPath);
    qint64 oldBytes = 0;
    for (const auto& run : oldRuns)
    {
        oldBytes = saturatingAddBytes(oldBytes, directorySizeBytes(run.absoluteFilePath()));
    }

    const auto exceedsHistoryPolicy = [&]() {
        return static_cast<int>(oldRuns.size()) + 1 > maxHistoryEntries ||
               saturatingAddBytes(oldBytes, knownCurrentRunBytes) > maxHistoryBytes;
    };

    while (exceedsHistoryPolicy() && !oldRuns.empty())
    {
        const QFileInfo oldest = oldRuns.front();
        const qint64 oldestBytes = directorySizeBytes(oldest.absoluteFilePath());
        oldBytes = (std::max)(static_cast<qint64>(0), oldBytes - oldestBytes);
        oldRuns.erase(oldRuns.begin());
    }

    if (exceedsHistoryPolicy())
    {
        return {false, QStringLiteral("the configured history retention policy cannot retain this run")};
    }

    return {true, {}};
}

constexpr QSize kMaximumReportPreviewSize{2048, 2048};

/// The bound of a calibration master's picture: a report shows it small, and a frame of noise at a master's bound is
/// a megabyte of JPEG in every run's archive.
constexpr QSize kMaximumCalibrationPreviewSize{1024, 1024};

[[nodiscard]] bool isValidPreparedReportPreview(const QByteArray& jpegBytes, const QSize& declaredSize)
{
    if (jpegBytes.isEmpty() || !declaredSize.isValid() || declaredSize.width() > kMaximumReportPreviewSize.width() ||
        declaredSize.height() > kMaximumReportPreviewSize.height())
    {
        return false;
    }

    QBuffer buffer;
    buffer.setData(jpegBytes);
    if (!buffer.open(QIODevice::ReadOnly))
    {
        return false;
    }

    QImageReader reader(&buffer, "jpeg");
    reader.setAutoTransform(false);
    if (!reader.canRead() || reader.size() != declaredSize)
    {
        return false;
    }

    const QImage decoded = reader.read();
    return !decoded.isNull() && decoded.size() == declaredSize;
}

[[nodiscard]] bool writeAtomicFile(const QString& path, const QByteArray& payload,
                                   QIODevice::OpenMode extraMode = QIODevice::NotOpen)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | extraMode))
    {
        return false;
    }

    if (file.write(payload) != payload.size())
    {
        file.cancelWriting();
        return false;
    }

    return file.commit();
}

[[nodiscard]] ImageIntegration::ArchiveFileTransportResult
snapshotSqliteDatabase(const QString& source_path, const QString& destination_path,
                       WorkflowExecutionCounterAccumulator* counters)
{
    ImageIntegration::ArchiveFileTransportResult result;
    if (!QFileInfo(source_path).isFile())
    {
        result.error = QStringLiteral("SQLite snapshot source does not exist");
        return result;
    }
    if (!QDir().mkpath(QFileInfo(destination_path).absolutePath()) ||
        (QFile::exists(destination_path) && !QFile::remove(destination_path)))
    {
        result.error = QStringLiteral("Could not prepare SQLite snapshot destination");
        return result;
    }

    core::SqliteConnection source(source_path, QStringLiteral("WorkflowArchiveSqliteSnapshot"));
    QString open_error;
    if (!source.open(&open_error))
    {
        result.error = open_error;
        return result;
    }

    QString escaped_destination = QDir::toNativeSeparators(QFileInfo(destination_path).absoluteFilePath());
    escaped_destination.replace(QLatin1Char('\''), QStringLiteral("''"));
    QSqlQuery snapshot_query(source.db());
    if (counters != nullptr)
        counters->recordSqliteStatementExecution();
    if (!snapshot_query.exec(QStringLiteral("VACUUM INTO '%1'").arg(escaped_destination)))
    {
        result.error = snapshot_query.lastError().text();
        QFile::remove(destination_path);
        return result;
    }

    core::SqliteConnection validation(destination_path, QStringLiteral("WorkflowArchiveSqliteValidation"));
    QSqlQuery integrity_query;
    if (!validation.open(&result.error))
    {
        QFile::remove(destination_path);
        return result;
    }
    integrity_query = QSqlQuery(validation.db());
    if (counters != nullptr)
        counters->recordSqliteStatementExecution();
    if (!integrity_query.exec(QStringLiteral("PRAGMA integrity_check")) || !integrity_query.next() ||
        integrity_query.value(0).toString() != QStringLiteral("ok"))
    {
        result.error = integrity_query.lastError().text().isEmpty() ? QStringLiteral("SQLite integrity check failed")
                                                                    : integrity_query.lastError().text();
        validation.close();
        QFile::remove(destination_path);
        return result;
    }

    const std::uint64_t snapshot_bytes =
        static_cast<std::uint64_t>((std::max)(qint64{0}, QFileInfo(destination_path).size()));
    if (counters != nullptr)
        counters->addBytesCopied(snapshot_bytes);
    result.outcome = ImageIntegration::ArchiveFileTransportOutcome::NonImmutableSourceCopySucceeded;
    result.diagnostics.ordinaryCopyBytes = snapshot_bytes;
    result.diagnostics.selectedTransport = ImageIntegration::ArchiveSelectedTransport::OrdinaryCopy;
    result.diagnostics.fallbackReason = ImageIntegration::ArchiveFallbackReason::NonImmutableSource;
    return result;
}

[[nodiscard]] QJsonObject workflowExecutionCountersToJson(const WorkflowExecutionCounters& counters)
{
    QJsonObject object;
    object[QStringLiteral("bytesHashed")] = QString::number(counters.bytesHashed());
    object[QStringLiteral("bytesCopied")] = QString::number(counters.bytesCopied());
    object[QStringLiteral("sqliteStatementExecutions")] = QString::number(counters.sqliteStatementExecutions());
    object[QStringLiteral("sqliteTransactionsStarted")] = QString::number(counters.sqliteTransactionsStarted());
    object[QStringLiteral("sqliteTransactionsCommitted")] = QString::number(counters.sqliteTransactionsCommitted());
    object[QStringLiteral("sqliteTransactionsRolledBack")] = QString::number(counters.sqliteTransactionsRolledBack());
    object[QStringLiteral("inputDecodeOperations")] = QString::number(counters.inputDecodeOperations());
    object[QStringLiteral("scientificOutputEncodeCompletions")] =
        QString::number(counters.scientificOutputEncodeCompletions());
    object[QStringLiteral("inputPassesStarted")] = QString::number(counters.inputPassesStarted());
    object[QStringLiteral("tilePassesStarted")] = QString::number(counters.tilePassesStarted());
    object[QStringLiteral("stackLoaderRequests")] = QString::number(counters.stackLoaderRequests());
    object[QStringLiteral("roiSubsetReads")] = QString::number(counters.roiSubsetReads());
    object[QStringLiteral("fullFrameFallbackReads")] = QString::number(counters.fullFrameFallbackReads());
    object[QStringLiteral("logicalDecodedPixelBytes")] = QString::number(counters.logicalDecodedPixelBytes());
    object[QStringLiteral("calibrationInvocations")] = QString::number(counters.calibrationInvocations());
    object[QStringLiteral("calibratedSpillFilesCreated")] = QString::number(counters.calibratedSpillFilesCreated());
    object[QStringLiteral("calibratedSpillBytesWritten")] = QString::number(counters.calibratedSpillBytesWritten());
    object[QStringLiteral("calibratedSpillBytesRead")] = QString::number(counters.calibratedSpillBytesRead());
    object[QStringLiteral("calibratedSpillRoiReads")] = QString::number(counters.calibratedSpillRoiReads());
    object[QStringLiteral("calibratedSpillCleanupSuccesses")] =
        QString::number(counters.calibratedSpillCleanupSuccesses());
    object[QStringLiteral("calibratedSpillCleanupFailures")] =
        QString::number(counters.calibratedSpillCleanupFailures());
    object[QStringLiteral("dumpBytesRead")] = QString::number(counters.dumpBytesRead());
    object[QStringLiteral("dumpBytesWritten")] = QString::number(counters.dumpBytesWritten());
    object[QStringLiteral("pixmapBytesRead")] = QString::number(counters.pixmapBytesRead());
    object[QStringLiteral("pixmapBytesWritten")] = QString::number(counters.pixmapBytesWritten());
    object[QStringLiteral("descriptorProviderHits")] = QString::number(counters.descriptorProviderHits());
    object[QStringLiteral("denseProviderHits")] = QString::number(counters.denseProviderHits());
    object[QStringLiteral("sipLazyProviderHits")] = QString::number(counters.sipLazyProviderHits());
    object[QStringLiteral("polynomialLazyProviderHits")] = QString::number(counters.polynomialLazyProviderHits());
    object[QStringLiteral("sipCoordinatesEvaluated")] = QString::number(counters.sipCoordinatesEvaluated());
    object[QStringLiteral("sharedSipPowerTableBuilds")] = QString::number(counters.sharedSipPowerTableBuilds());
    object[QStringLiteral("explicitSipInverseEvaluations")] = QString::number(counters.explicitSipInverseEvaluations());
    object[QStringLiteral("sipNewtonFallbackIterations")] = QString::number(counters.sipNewtonFallbackIterations());
    object[QStringLiteral("polynomialCoordinatesEvaluated")] =
        QString::number(counters.polynomialCoordinatesEvaluated());
    object[QStringLiteral("sharedPolynomialPowerTableBuilds")] =
        QString::number(counters.sharedPolynomialPowerTableBuilds());
    object[QStringLiteral("lazyBlocksGenerated")] = QString::number(counters.lazyBlocksGenerated());
    object[QStringLiteral("lazyCoordinatePairsGenerated")] = QString::number(counters.lazyCoordinatePairsGenerated());
    object[QStringLiteral("peakLazyCoordinateBytes")] = QString::number(counters.peakLazyCoordinateBytes());
    object[QStringLiteral("denseFallbackCount")] = QString::number(counters.denseFallbackCount());
    object[QStringLiteral("lazyMapCancellations")] = QString::number(counters.lazyMapCancellations());
    object[QStringLiteral("lazyMapFailures")] = QString::number(counters.lazyMapFailures());
    return object;
}

/// The most the pictures of earlier runs a report copies beside its masters' own can weigh: for each master the run
/// made, the largest pair of pictures an earlier run of the folder keeps of it. A report copies the pair of the run
/// each master is compared with, after the payloads are admitted.
[[nodiscard]] qint64 earlierPicturesBound(const std::vector<QFileInfo>& earlierRuns,
                                          const std::vector<QString>& groupFolders)
{
    qint64 total = 0;
    for (const QString& folder : groupFolders)
    {
        qint64 largest = 0;
        for (const QFileInfo& run : earlierRuns)
        {
            const QDir pictures(
                QDir(run.absoluteFilePath()).filePath(QStringLiteral("report_previews/%1").arg(folder)));
            qint64 pair = 0;
            for (const QString& name : {QStringLiteral("master.jpg"), QStringLiteral("detail.jpg")})
            {
                const QFileInfo picture(pictures.filePath(name));
                if (picture.isFile())
                {
                    pair = saturatingAddBytes(pair, picture.size());
                }
            }
            largest = (std::max)(largest, pair);
        }
        total = saturatingAddBytes(total, largest);
    }
    return total;
}

/// What an archive states of when, where and by which version it was written, from the host.
[[nodiscard]] WorkflowArchiver::ArchiveStamp hostArchiveStamp()
{
    return WorkflowArchiver::ArchiveStamp{.archivedAt = QDateTime::currentDateTime(),
                                          .timeZoneId = QString::fromUtf8(QTimeZone::systemTimeZoneId()),
                                          .applicationVersion =
                                              qApp != nullptr ? qApp->applicationVersion() : QString()};
}

/// The file a staged copy was staged from: the staged path less its ".tmp_archive_<uuid>" suffix, or an empty string
/// for a path that has no such suffix.
[[nodiscard]] QString originalPathForStagedArchivePath(const QString& stagedPath)
{
    const QString uuidSuffixPrefix = QStringLiteral(".tmp_archive_");
    const int suffixStart = stagedPath.lastIndexOf(uuidSuffixPrefix);
    if (suffixStart < 0)
    {
        return {};
    }

    const QString uuidText = stagedPath.mid(suffixStart + uuidSuffixPrefix.size());
    if (uuidText.isEmpty())
    {
        return {};
    }

    const QUuid uuid(QStringLiteral("{%1}").arg(uuidText));
    if (uuid.isNull())
    {
        return {};
    }

    return stagedPath.left(suffixStart);
}

std::vector<RejectionMapResult> detectRejectionMapsForOutput(const QString& outputPath)
{
    std::vector<RejectionMapResult> maps;

    const QFileInfo outInfo(outputPath);
    if (!outInfo.exists())
    {
        return maps;
    }

    QSet<QString> seen;
    const ArtifactTracker tracker(outInfo.absolutePath());
    auto appendSpecs = [&](const QVector<ImageIntegration::RejectionMapSpec>& specs) {
        for (const auto& spec : specs)
        {
            const QString mapPath = outInfo.dir().filePath(spec.filename);
            const QFileInfo mapInfo(mapPath);
            if (!mapInfo.exists())
            {
                continue;
            }
            if (!ImageIntegration::isStackingAuxiliaryOutputCurrent(tracker, outInfo.absoluteFilePath(), mapPath))
            {
                continue;
            }
            if (seen.contains(mapInfo.absoluteFilePath()))
            {
                continue;
            }
            seen.insert(mapInfo.absoluteFilePath());
            maps.push_back(RejectionMapResult{spec.title, mapInfo.absoluteFilePath(), {}});
        }
    };

    appendSpecs(ImageIntegration::rejectionMapsForOutput(outInfo, true));
    appendSpecs(ImageIntegration::rejectionMapsForOutput(outInfo, false));

    return maps;
}

std::vector<QualityMapResult> detectQualityMapsForOutput(const QString& outputPath)
{
    std::vector<QualityMapResult> maps;

    const QFileInfo outInfo(outputPath);
    if (!outInfo.exists())
    {
        return maps;
    }

    QSet<QString> seen;
    const ArtifactTracker tracker(outInfo.absolutePath());
    const auto specs = ImageIntegration::qualityMapsForOutput(outInfo, true);
    for (const auto& spec : specs)
    {
        const QString mapPath = outInfo.dir().filePath(spec.filename);
        const QFileInfo mapInfo(mapPath);
        if (!mapInfo.exists())
        {
            continue;
        }
        if (!ImageIntegration::isStackingAuxiliaryOutputCurrent(tracker, outInfo.absoluteFilePath(), mapPath))
        {
            continue;
        }
        if (seen.contains(mapInfo.absoluteFilePath()))
        {
            continue;
        }
        seen.insert(mapInfo.absoluteFilePath());
        maps.push_back(QualityMapResult{spec.title, mapInfo.absoluteFilePath(), {}});
    }

    return maps;
}

[[nodiscard]] RejectionClusterMapKind rejectionMapKindForTitle(const QString& title)
{
    const QString normalized = title.trimmed().toLower();
    if (normalized.contains(QStringLiteral("low")))
    {
        return RejectionClusterMapKind::Low;
    }
    if (normalized.contains(QStringLiteral("high")))
    {
        return RejectionClusterMapKind::High;
    }
    return RejectionClusterMapKind::Merged;
}

/// The integration group results of an earlier run's archive and the folders it keeps their pictures in.
struct EarlierRunPictures
{
    QString runPath;
    QJsonArray groupResults;
    std::vector<QString> groupKeys;
    std::vector<QString> folders;
};

/// Reads what an earlier run's archive states of its groups; nothing for an archive whose manifest does not read.
[[nodiscard]] std::optional<EarlierRunPictures> readEarlierRunPictures(const QString& runPath)
{
    QFile file(QDir(runPath).filePath(QStringLiteral("workflow_manifest.json")));
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
    {
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    EarlierRunPictures run;
    run.runPath = runPath;
    run.groupResults = root.value(QStringLiteral("integrationGroupResults")).toArray();
    // The folders are named over the archive's results and then its reused groups, as it named them.
    for (const auto& value : run.groupResults)
    {
        run.groupKeys.push_back(value.toObject().value(QStringLiteral("groupKey")).toString());
    }
    std::vector<QString> named = run.groupKeys;
    for (const ReusedIntegrationGroup& reused :
         reusedIntegrationGroupsFromJson(root.value(QStringLiteral("reusedIntegrationGroups")).toArray()))
    {
        named.push_back(reused.groupKey);
    }
    run.folders = ImageIntegration::archiveGroupFolderNames(named);
    return run;
}

/// Copies a reused master's pictures from the archive of the run that made it: the latest earlier run of the history
/// whose result for the group succeeded. The master on disk must still be the size that run recorded, and that run
/// must keep the whole-frame picture; the detail is copied when it keeps one.
///
/// @param earlierRuns the history's earlier runs, latest first; an entry is read the first time it is asked for.
/// @return whether the whole-frame picture was copied.
[[nodiscard]] bool
copyPicturesOfTheRunThatMade(std::vector<std::pair<QString, std::optional<EarlierRunPictures>>>& earlierRuns,
                             const ReusedIntegrationGroup& reused, const QString& destinationDir)
{
    for (auto& [runPath, pictures] : earlierRuns)
    {
        if (runPath.isEmpty())
        {
            continue;
        }
        if (!pictures.has_value())
        {
            pictures = readEarlierRunPictures(runPath);
            if (!pictures.has_value())
            {
                // An archive that does not read is not asked again.
                runPath.clear();
                continue;
            }
        }
        for (std::size_t index = 0; index < pictures->groupKeys.size(); ++index)
        {
            if (pictures->groupKeys[index] != reused.groupKey)
            {
                continue;
            }
            const QJsonObject made = pictures->groupResults.at(static_cast<qsizetype>(index)).toObject();
            if (made.value(QStringLiteral("result")).toString() != QStringLiteral("success"))
            {
                continue;
            }
            // The latest run that made the master is the one to copy from, or none is.
            const qint64 recordedBytes = made.value(QStringLiteral("outputBytes")).toVariant().toLongLong();
            const QFileInfo master(reused.outputPath);
            const QDir source(
                QDir(pictures->runPath).filePath(QStringLiteral("report_previews/%1").arg(pictures->folders[index])));
            const QString whole = source.filePath(QStringLiteral("master.jpg"));
            if (!master.exists() || master.size() != recordedBytes || !QFile::exists(whole))
            {
                return false;
            }
            if (!QDir().mkpath(destinationDir) ||
                !QFile::copy(whole, QDir(destinationDir).filePath(QStringLiteral("master.jpg"))))
            {
                return false;
            }
            const QString detail = source.filePath(QStringLiteral("detail.jpg"));
            if (QFile::exists(detail))
            {
                (void)QFile::copy(detail, QDir(destinationDir).filePath(QStringLiteral("detail.jpg")));
            }
            return true;
        }
    }
    return false;
}

void applySpatialClusterOverlay(const QString& previewPath, const StackQualityRecord& qualityReport,
                                const RejectionClusterMapKind mapKind)
{
    if (!qualityReport.hasSpatialRejectionClusters || qualityReport.spatialClusters.empty())
    {
        return;
    }

    QImage preview(previewPath);
    if (preview.isNull())
    {
        return;
    }

    // Intentionally fixed colours, NOT theme tokens. These are painted into a PNG that is baked into
    // the archived workflow report, so they have to stay stable and legible on their own: the file
    // outlives the session, is read outside the app (and printed), and would otherwise encode
    // whichever theme happened to be active when the run finished. Two reports of the same data
    // must not differ because the user switched themes between them. This is not themed UI - do not
    // route it through an::ui::tokens().
    QColor stroke = QColor(255, 170, 0);
    if (mapKind == RejectionClusterMapKind::Low)
    {
        stroke = QColor(64, 224, 255);
    }
    else if (mapKind == RejectionClusterMapKind::High)
    {
        stroke = QColor(255, 96, 96);
    }

    QPainter painter(&preview);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(stroke);
    pen.setWidth(2);
    painter.setPen(pen);

    int labels_drawn = 0;
    for (const auto& cluster : qualityReport.spatialClusters)
    {
        if (cluster.mapKind != mapKind)
        {
            continue;
        }

        const QRect rect(QPoint(cluster.minX, cluster.minY), QPoint(cluster.maxX, cluster.maxY));
        painter.drawRect(rect);

        if (labels_drawn < 6)
        {
            const QString label = QStringLiteral("%1 px").arg(static_cast<qulonglong>(cluster.pixelCount));
            const QRect textRect(cluster.minX, (std::max)(0, cluster.minY - 16), 72, 14);
            painter.fillRect(textRect.adjusted(-1, -1, 1, 1), QColor(0, 0, 0, 160));
            painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, label);
            ++labels_drawn;
        }
    }

    preview.save(previewPath);
}
} // namespace

namespace ImageIntegration
{
bool runHasWorkflowReport(const WorkflowManifest& manifest, const WorkflowExecutor::Result& result)
{
    const QString scope = manifest.executionScope.trimmed().toLower();
    if (manifest.executionMode == ExecutionMode::RegistrationOnly || scope == QStringLiteral("registration_all") ||
        scope == QStringLiteral("registration_group"))
    {
        return false;
    }

    if (scope.isEmpty() || scope == QStringLiteral("full") || scope == QStringLiteral("integration_group"))
    {
        return true;
    }

    return !manifest.integrationGroups.empty() || !result.integrationGroupResults.empty();
}

qint64 estimatedArchiveMetadataBytes(const WorkflowManifest& manifest, const WorkflowExecutor::Result& result)
{
    return pendingMandatoryMetadataBytes(manifest, result);
}

std::vector<QString> archiveGroupFolderNames(const std::vector<QString>& groupKeys)
{
    QHash<QString, int> baseCounts;
    for (const QString& key : groupKeys)
    {
        ++baseCounts[sanitizeFilename(key)];
    }
    std::vector<QString> folders;
    folders.reserve(groupKeys.size());
    QSet<QString> used;
    for (const QString& key : groupKeys)
    {
        const QString base = sanitizeFilename(key);
        QString folder = base;
        if (baseCounts.value(base, 0) > 1)
        {
            const QString hash = stableShortHash(key, 8);
            folder = QStringLiteral("%1_%2").arg(base, hash);
            for (int index = 2; used.contains(folder); ++index)
            {
                folder = QStringLiteral("%1_%2_%3").arg(base, hash).arg(index);
            }
            LOG_WARNING("WorkflowArchiver: group directory collision detected for '{}'; using '{}'", key.toStdString(),
                        folder.toStdString());
        }
        used.insert(folder);
        folders.push_back(folder);
    }
    return folders;
}

std::vector<ArchivedCalibrationPreview> archivedCalibrationPreviews(const WorkflowManifest& manifest)
{
    std::vector<ArchivedCalibrationPreview> previews;
    const auto collect = [&](const std::optional<WorkflowManifest::CalibrationStage>& stage, const QString& type) {
        if (!stage.has_value())
        {
            return;
        }
        for (const auto& group : stage->groups)
        {
            ArchivedCalibrationPreview preview;
            preview.type = type;
            preview.groupKey = group.groupKey.toString();
            if (group.existingMaster.has_value())
            {
                preview.masterPath = group.existingMaster->canonicalPath();
            }
            else if (!group.outputFilename.trimmed().isEmpty() && !manifest.outputDirectory.trimmed().isEmpty())
            {
                preview.masterPath =
                    QFileInfo(QDir(manifest.outputDirectory).filePath(group.outputFilename)).absoluteFilePath();
            }
            previews.push_back(std::move(preview));
        }
    };
    collect(manifest.biasStage, QStringLiteral("Bias"));
    collect(manifest.darkStage, QStringLiteral("Dark"));
    collect(manifest.flatStage, QStringLiteral("Flat"));

    const auto baseOf = [](const ArchivedCalibrationPreview& preview) {
        const QString base = sanitizeFilename(QFileInfo(preview.masterPath).completeBaseName());
        return base.isEmpty() ? sanitizeFilename(preview.type.toLower()) : base;
    };
    QHash<QString, int> baseCounts;
    for (const ArchivedCalibrationPreview& preview : previews)
    {
        ++baseCounts[baseOf(preview).toLower()];
    }
    QSet<QString> used;
    for (ArchivedCalibrationPreview& preview : previews)
    {
        const QString base = baseOf(preview);
        QString name = base;
        if (baseCounts.value(base.toLower(), 0) > 1)
        {
            const QString hash =
                stableShortHash(QStringLiteral("%1|%2").arg(preview.type.toLower(), preview.groupKey), 8);
            name = QStringLiteral("%1_%2").arg(base, hash);
            for (int index = 2; used.contains(name.toLower()); ++index)
            {
                name = QStringLiteral("%1_%2_%3").arg(base, hash).arg(index);
            }
        }
        used.insert(name.toLower());
        preview.fileName = name + QStringLiteral(".jpg");
    }
    return previews;
}
} // namespace ImageIntegration

WorkflowArchiver::WorkflowArchiver(const QString& outputDir, WorkflowExecutionCounterAccumulator* counters)
    : m_outputDir(outputDir), m_maxHistoryEntries(ImageIntegration::Config::kRunHistoryMaxEntries),
      m_maxHistoryBytes(static_cast<qint64>(ImageIntegration::Config::kRunHistoryMaxTotalBytes)), m_counters(counters)
{
}

WorkflowArchiver::WorkflowArchiver(const QString& outputDir, WorkflowExecutionCounterAccumulator* counters,
                                   ImageIntegration::ArchiveFileTransport archiveTransport)
    : m_outputDir(outputDir), m_maxHistoryEntries(ImageIntegration::Config::kRunHistoryMaxEntries),
      m_maxHistoryBytes(static_cast<qint64>(ImageIntegration::Config::kRunHistoryMaxTotalBytes)), m_counters(counters),
      m_archiveTransport(std::move(archiveTransport))
{
}

WorkflowArchiver::WorkflowArchiver(const QString& outputDir, WorkflowExecutionCounterAccumulator* counters,
                                   WorkflowLogWriter workflowLogWriter)
    : m_outputDir(outputDir), m_maxHistoryEntries(ImageIntegration::Config::kRunHistoryMaxEntries),
      m_maxHistoryBytes(static_cast<qint64>(ImageIntegration::Config::kRunHistoryMaxTotalBytes)), m_counters(counters),
      m_workflowLogWriter(std::move(workflowLogWriter))
{
}

WorkflowArchiver::WorkflowArchiver(const QString& outputDir, int maxHistoryEntries, qint64 maxHistoryBytes,
                                   WorkflowExecutionCounterAccumulator* counters)
    : m_outputDir(outputDir), m_maxHistoryEntries((std::max)(1, maxHistoryEntries)),
      m_maxHistoryBytes((std::max)(static_cast<qint64>(1), maxHistoryBytes)), m_counters(counters)
{
}

WorkflowArchiver::WorkflowArchiver(const QString& outputDir, const int maxHistoryEntries, const qint64 maxHistoryBytes,
                                   WorkflowExecutionCounterAccumulator* counters,
                                   ImageIntegration::ArchiveFileTransport archiveTransport)
    : m_outputDir(outputDir), m_maxHistoryEntries((std::max)(1, maxHistoryEntries)),
      m_maxHistoryBytes((std::max)(static_cast<qint64>(1), maxHistoryBytes)), m_counters(counters),
      m_archiveTransport(std::move(archiveTransport))
{
}

void WorkflowArchiver::setEngineSession(const an::consumer::EngineSession* session)
{
    m_session = session;
}

void WorkflowArchiver::setArchiveStamp(ArchiveStamp stamp)
{
    m_archiveStamp = std::move(stamp);
}

int WorkflowArchiver::cleanupOrphanedStagedFiles(const QString& outputDir)
{
    const QString trimmedOutputDir = outputDir.trimmed();
    if (trimmedOutputDir.isEmpty())
    {
        return 0;
    }

    const QDir root(trimmedOutputDir);
    if (!root.exists())
    {
        return 0;
    }

    const QString kStagedSuffix = QStringLiteral(".tmp_archive");
    const QString historyPrefix = QDir(trimmedOutputDir).filePath(QStringLiteral("history/"));
    int processed = 0;

    QDirIterator it(trimmedOutputDir, QStringList{QStringLiteral("*%1*").arg(kStagedSuffix)}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString stagedPath = it.next();
        if (stagedPath.startsWith(historyPrefix))
        {
            continue;
        }

        const QString originalPath = originalPathForStagedArchivePath(stagedPath);
        if (originalPath.isEmpty())
        {
            continue;
        }

        const bool originalExists = QFileInfo::exists(originalPath);

        if (originalExists)
        {
            if (!QFile::remove(stagedPath))
            {
                LOG_WARNING("WorkflowArchiver: failed to remove orphaned staged file '{}'", stagedPath.toStdString());
                continue;
            }
            ++processed;
            continue;
        }

        if (QFile::rename(stagedPath, originalPath))
        {
            ++processed;
            continue;
        }
        if (copyWorkflowFile(stagedPath, originalPath))
        {
            QFile::remove(stagedPath);
            ++processed;
            continue;
        }

        LOG_WARNING("WorkflowArchiver: failed to restore orphaned staged file '{}' to '{}'", stagedPath.toStdString(),
                    originalPath.toStdString());
    }

    if (processed > 0)
    {
        LOG_INFO("WorkflowArchiver: cleaned {} orphaned staged file(s)", processed);
    }
    return processed;
}

WorkflowArchiver::ArchiveResult WorkflowArchiver::commit(const WorkflowManifest& manifest,
                                                         WorkflowExecutor::Result& result)
{
    auto last_boundary = std::chrono::steady_clock::now();
    const auto capture_phase_duration = [&last_boundary]() {
        const auto current = std::chrono::steady_clock::now();
        const qint64 duration_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(current - last_boundary).count();
        last_boundary = current;
        return (std::max)(static_cast<qint64>(0), duration_ms);
    };
    ArchiveResult archiveResult = commit(manifest, result, capture_phase_duration);
    WorkflowExecutor::Result finalizedResult = result;
    if (m_counters != nullptr)
    {
        finalizedResult.executionCounters = m_counters->snapshot();
    }
    finalizeTimingArtifacts(finalizedResult);
    return archiveResult;
}

WorkflowArchiver::ArchiveResult WorkflowArchiver::commit(const WorkflowManifest& manifest,
                                                         const WorkflowExecutor::Result& result)
{
    WorkflowExecutor::Result mutableResult = result;
    return commit(manifest, mutableResult);
}

WorkflowArchiver::ArchiveResult WorkflowArchiver::commit(const WorkflowManifest& manifest,
                                                         WorkflowExecutor::Result& result,
                                                         const CapturePhaseDuration& capturePhaseDuration)
{
    ArchiveResult archiveResult;
    m_runDirectory.clear();
    m_reportModel.reset();
    m_manifestPath.clear();
    m_logPath.clear();

    const auto finish_archive_phase = [&]() { archiveResult.archiveMs += capturePhaseDuration(); };

    if (m_outputDir.isEmpty())
    {
        archiveResult.error = "Output directory is empty.";
        finish_archive_phase();
        return archiveResult;
    }

    // 1. Create history/executionId directory
    QDir outputRoot(m_outputDir);
    if (!outputRoot.exists())
    {
        outputRoot.mkpath(".");
    }

    const QString historyPath = outputRoot.filePath("history");
    QDir historyDir(historyPath);
    if (!historyDir.exists() && !historyDir.mkpath("."))
    {
        archiveResult.error = "Failed to create history directory.";
        finish_archive_phase();
        return archiveResult;
    }

    const QString executionDirName = manifest.executionId;
    if (!isSafeArchivePathComponent(executionDirName))
    {
        archiveResult.error = "Execution ID is missing or contains unsafe path characters.";
        finish_archive_phase();
        return archiveResult;
    }

    const QString runDirPath = historyDir.filePath(executionDirName);
    if (!isPathContainedBy(historyPath, runDirPath))
    {
        archiveResult.error = "Execution archive path escapes the history directory.";
        finish_archive_phase();
        return archiveResult;
    }
    if (QFileInfo::exists(runDirPath))
    {
        archiveResult.error = "Execution archive directory already exists.";
        finish_archive_phase();
        return archiveResult;
    }
    QDir runDir(runDirPath);
    if (!historyDir.mkdir(executionDirName) || !runDir.exists())
    {
        archiveResult.error = "Failed to create run directory.";
        finish_archive_phase();
        return archiveResult;
    }
    archiveResult.archivePath = runDirPath;
    const auto abort_archive_transaction = [&](const QString& error) {
        archiveResult.error = error;
        archiveResult.success = false;
        revertStagedFiles(result);
        if (QFileInfo::exists(runDirPath) && !QDir(runDirPath).removeRecursively())
        {
            LOG_WARNING("WorkflowArchiver: Failed to remove incomplete archive {}", runDirPath.toStdString());
        }
        m_manifestPath.clear();
        m_runDirectory.clear();
        m_reportModel.reset();
        m_logPath.clear();
    };
    const WorkflowManifest persistedManifest = manifest.redactedForPersistence();

    // 2. Materialize previews and collect archive metadata before payload retention is decided.
    WorkflowExecutor::Result reportResult = result;
    int itemsArchived = 0;
    bool hasStagedFiles = false;
    QHash<QString, QJsonArray> archivedFilesByGroup;
    QHash<QString, QString> archivedPreviewByGroup;
    QHash<QString, QJsonArray> archivedRejectionMapsByGroup;
    QHash<QString, QJsonArray> archivedQualityMapsByGroup;
    std::vector<QString> groupDirNames;
    groupDirNames.reserve(result.integrationGroupResults.size());

    auto moveFile = [this](const QString& src, const QString& dst) -> bool {
        if (QFile::exists(dst))
            QFile::remove(dst);

        QFileInfo dstInfo(dst);
        QDir dstDir = dstInfo.dir();
        if (!dstDir.exists())
            dstDir.mkpath(".");

        if (QFile::rename(src, dst))
            return true;
        if (copyWorkflowFile(src, dst, m_counters))
        {
            QFile::remove(src);
            return true;
        }
        return false;
    };

    // The groups the archive names a folder for: the ones the run has a result for, then the ones it reused, which
    // have none.
    std::vector<QString> archivedGroupKeys;
    archivedGroupKeys.reserve(result.integrationGroupResults.size() + result.reusedIntegrationGroups.size());
    for (const auto& groupResult : result.integrationGroupResults)
    {
        archivedGroupKeys.push_back(groupResult.groupKey);
    }
    std::vector<const ReusedIntegrationGroup*> reusedGroups;
    for (const ReusedIntegrationGroup& reused : result.reusedIntegrationGroups)
    {
        if (std::find(archivedGroupKeys.begin(), archivedGroupKeys.end(), reused.groupKey) == archivedGroupKeys.end())
        {
            archivedGroupKeys.push_back(reused.groupKey);
            reusedGroups.push_back(&reused);
        }
    }
    const std::vector<QString> archivedGroupFolders = ImageIntegration::archiveGroupFolderNames(archivedGroupKeys);

    for (int groupIndex = 0; groupIndex < static_cast<int>(result.integrationGroupResults.size()); ++groupIndex)
    {
        auto& groupResult = result.integrationGroupResults[groupIndex];
        auto& reportGroupResult = reportResult.integrationGroupResults[groupIndex];

        const QString groupDirName = archivedGroupFolders[static_cast<std::size_t>(groupIndex)];
        groupDirNames.push_back(groupDirName);
        const QString groupDirPath = runDir.filePath("groups/" + groupDirName);
        QDir groupDir(groupDirPath);
        if (!groupDir.exists())
        {
            groupDir.mkpath(".");
        }

        const QString reportPreviewDirPath = runDir.filePath(QStringLiteral("report_previews/%1").arg(groupDirName));
        QDir reportPreviewDir(reportPreviewDirPath);
        if (!reportPreviewDir.exists())
        {
            reportPreviewDir.mkpath(QStringLiteral("."));
        }

        QJsonArray archivedFilesJson;

        if (!groupResult.previewPath.trimmed().isEmpty() && QFile::exists(groupResult.previewPath))
        {
            const QString previewDest = groupDir.filePath("preview.png");
            if (moveFile(groupResult.previewPath, previewDest))
            {
                const QString archivedPreviewPath = QStringLiteral("groups/%1/preview.png").arg(groupDirName);
                archivedPreviewByGroup.insert(groupResult.groupKey, archivedPreviewPath);
                reportGroupResult.previewPath = archivedPreviewPath;
            }
            else
            {
                LOG_WARNING("WorkflowArchiver: Failed to move preview {} to {}", groupResult.previewPath.toStdString(),
                            previewDest.toStdString());
            }
        }

        // Reuse P1-02's compact resident-master JPEG exactly when it is valid.
        // Compatibility fallback is bounded so it cannot recreate the old full-resolution allocation.
        const QString reportPreviewPath = reportPreviewDir.filePath(QStringLiteral("master.jpg"));
        const bool preparedPreviewIsValid =
            isValidPreparedReportPreview(groupResult.reportPreviewJpeg, groupResult.reportPreviewSize);
        if (preparedPreviewIsValid)
        {
            if (!writeAtomicFile(reportPreviewPath, groupResult.reportPreviewJpeg))
            {
                LOG_WARNING("WorkflowArchiver: Failed to write prepared report preview for group {}",
                            groupResult.groupKey.toStdString());
            }
            // The detail the run prepared beside it: the master's centre at full scale, stretched as the whole
            // frame is. One that is not a picture of at most the detail's size is not written.
            if (isValidPreparedReportPreview(groupResult.reportDetailJpeg, groupResult.reportDetailSize) &&
                groupResult.reportDetailSize.width() <= ImageIntegration::kReportDetailRegionSize.width() &&
                groupResult.reportDetailSize.height() <= ImageIntegration::kReportDetailRegionSize.height())
            {
                if (!writeAtomicFile(reportPreviewDir.filePath(QStringLiteral("detail.jpg")),
                                     groupResult.reportDetailJpeg))
                {
                    LOG_WARNING("WorkflowArchiver: Failed to write prepared report detail for group {}",
                                groupResult.groupKey.toStdString());
                }
            }
            else if (!groupResult.reportDetailJpeg.isEmpty())
            {
                LOG_WARNING("WorkflowArchiver: Ignoring invalid prepared report detail for group {}",
                            groupResult.groupKey.toStdString());
            }
        }
        else if (!groupResult.reportPreviewJpeg.isEmpty())
        {
            LOG_WARNING("WorkflowArchiver: Ignoring invalid prepared report preview for group {}",
                        groupResult.groupKey.toStdString());
        }

        // Without a session no image is read: the report shows the previews the run prepared and nothing else.
        if (m_session != nullptr && !preparedPreviewIsValid && !groupResult.reportPreviewPreparedForCurrentRun &&
            !groupResult.outputPath.trimmed().isEmpty() && QFile::exists(groupResult.outputPath))
        {
            // One read of the master gives both of its pictures.
            (void)ImageIntegration::generateReportMasterPreviews(
                *m_session, groupResult.outputPath, reportPreviewDirPath, QStringLiteral("master.jpg"),
                QStringLiteral("detail.jpg"), kMaximumReportPreviewSize);
        }
        groupResult.reportPreviewJpeg.clear();
        groupResult.reportPreviewSize = {};
        groupResult.reportDetailJpeg.clear();
        groupResult.reportDetailSize = {};
        reportGroupResult.reportPreviewJpeg.clear();
        reportGroupResult.reportPreviewSize = {};
        reportGroupResult.reportDetailJpeg.clear();
        reportGroupResult.reportDetailSize = {};

        archivedFilesByGroup.insert(groupResult.groupKey, archivedFilesJson);

        std::vector<RejectionMapResult> rejectionMaps = groupResult.rejectionMaps;
        if (rejectionMaps.empty() && groupResult.success)
        {
            rejectionMaps = detectRejectionMapsForOutput(groupResult.outputPath);
        }

        QJsonArray rejectionMapsJson;
        std::vector<RejectionMapResult> reportRejectionMaps;
        reportRejectionMaps.reserve(rejectionMaps.size());

        int rejectionPreviewIndex = 1;
        for (auto rejectionMap : rejectionMaps)
        {
            QJsonObject rejectionJson;
            rejectionJson["title"] = rejectionMap.title;
            rejectionJson["path"] = rejectionMap.path;

            const QFileInfo mapInfo(rejectionMap.path);
            if (mapInfo.exists())
            {
                rejectionJson["sizeBytes"] = mapInfo.size();

                // Report-grade rejection map preview JPEG, bounded like the master preview.
                const QString previewName = QStringLiteral("rejection_map_%1.jpg").arg(rejectionPreviewIndex);
                if (m_session != nullptr &&
                    ImageIntegration::generateReportPreviewJpeg(*m_session, rejectionMap.path, reportPreviewDirPath,
                                                                previewName, kMaximumReportPreviewSize))
                {
                    applySpatialClusterOverlay(QDir(reportPreviewDirPath).filePath(previewName),
                                               groupResult.qualityReport, rejectionMapKindForTitle(rejectionMap.title));
                }
            }

            reportRejectionMaps.push_back(std::move(rejectionMap));
            rejectionMapsJson.append(rejectionJson);
            ++rejectionPreviewIndex;
        }

        reportGroupResult.rejectionMaps = std::move(reportRejectionMaps);
        archivedRejectionMapsByGroup.insert(groupResult.groupKey, rejectionMapsJson);

        std::vector<QualityMapResult> qualityMaps = groupResult.qualityMaps;
        if (qualityMaps.empty() && groupResult.success)
        {
            qualityMaps = detectQualityMapsForOutput(groupResult.outputPath);
        }

        QJsonArray qualityMapsJson;
        std::vector<QualityMapResult> reportQualityMaps;
        reportQualityMaps.reserve(qualityMaps.size());

        int qualityPreviewIndex = 1;
        for (auto qualityMap : qualityMaps)
        {
            QJsonObject qualityJson;
            qualityJson["title"] = qualityMap.title;
            qualityJson["path"] = qualityMap.path;

            const QFileInfo mapInfo(qualityMap.path);
            if (mapInfo.exists())
            {
                qualityJson["sizeBytes"] = mapInfo.size();

                if (m_session != nullptr)
                {
                    ImageIntegration::generateReportPreviewJpeg(
                        *m_session, qualityMap.path, reportPreviewDirPath,
                        QStringLiteral("quality_map_%1.jpg").arg(qualityPreviewIndex), kMaximumReportPreviewSize);
                }
            }

            reportQualityMaps.push_back(std::move(qualityMap));
            qualityMapsJson.append(qualityJson);
            ++qualityPreviewIndex;
        }

        reportGroupResult.qualityMaps = std::move(reportQualityMaps);
        archivedQualityMapsByGroup.insert(groupResult.groupKey, qualityMapsJson);
    }

    // The pictures a report shows that no result brings: a reused master's, copied from the archive of the run that
    // made it or made from one read of the file, and each calibration master's. They are written before the run's
    // size is measured, so the retention decision counts them; a preview is a picture, never a workflow image.
    const bool hasReport = ImageIntegration::runHasWorkflowReport(persistedManifest, result);
    if (hasReport)
    {
        std::vector<std::pair<QString, std::optional<EarlierRunPictures>>> earlierRuns;
        if (!reusedGroups.empty())
        {
            const std::vector<QFileInfo> runs = sortedHistoryRunDirectories(historyDir, runDirPath);
            for (auto run = runs.rbegin(); run != runs.rend(); ++run)
            {
                earlierRuns.emplace_back(run->absoluteFilePath(), std::nullopt);
            }
        }
        for (std::size_t index = 0; index < reusedGroups.size(); ++index)
        {
            const ReusedIntegrationGroup& reused = *reusedGroups[index];
            const QString folder = archivedGroupFolders[result.integrationGroupResults.size() + index];
            const QString picturesDir = runDir.filePath(QStringLiteral("report_previews/%1").arg(folder));
            if (copyPicturesOfTheRunThatMade(earlierRuns, reused, picturesDir))
            {
                continue;
            }
            if (m_session != nullptr && !reused.outputPath.trimmed().isEmpty() && QFile::exists(reused.outputPath))
            {
                (void)ImageIntegration::generateReportMasterPreviews(
                    *m_session, reused.outputPath, picturesDir, QStringLiteral("master.jpg"),
                    QStringLiteral("detail.jpg"), kMaximumReportPreviewSize);
            }
        }

        if (m_session != nullptr)
        {
            const QString calibrationDir = runDir.filePath(QStringLiteral("report_previews/calibration"));
            for (const ImageIntegration::ArchivedCalibrationPreview& preview :
                 ImageIntegration::archivedCalibrationPreviews(persistedManifest))
            {
                if (!preview.masterPath.isEmpty() && QFile::exists(preview.masterPath))
                {
                    (void)ImageIntegration::generateReportPreviewJpeg(*m_session, preview.masterPath, calibrationDir,
                                                                      preview.fileName, kMaximumCalibrationPreviewSize);
                }
            }
        }
    }

    qint64 knownCurrentRunBytes = directorySizeBytes(runDirPath);
    QSet<QString> accountedPayloadPaths;
    const auto accountKnownPayload = [&knownCurrentRunBytes, &accountedPayloadPaths](const QString& path) {
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile())
        {
            return;
        }

        const QString normalizedPath = QDir::cleanPath(info.absoluteFilePath());
        if (accountedPayloadPaths.contains(normalizedPath))
        {
            return;
        }
        accountedPayloadPaths.insert(normalizedPath);
        knownCurrentRunBytes =
            saturatingAddBytes(knownCurrentRunBytes, (std::max)(static_cast<qint64>(0), info.size()));
    };

    for (const auto& groupResult : result.integrationGroupResults)
    {
        if (groupResult.success)
        {
            accountKnownPayload(groupResult.outputPath);
        }
        for (const auto& staged : groupResult.stagedFiles)
        {
            if (QFileInfo::exists(staged.stagedPath))
            {
                hasStagedFiles = true;
                accountKnownPayload(staged.stagedPath);
            }
        }
    }

    const QString metricsSnapshotSource = outputRoot.filePath(QStringLiteral("registration_metrics.db"));
    // VACUUM INTO includes committed pages that have not yet been checkpointed from the WAL.
    // Count the WAL sidecar before deciding whether this run fits the retention budget.
    accountKnownPayload(metricsSnapshotSource);
    accountKnownPayload(metricsSnapshotSource + QStringLiteral("-wal"));
    knownCurrentRunBytes =
        saturatingAddBytes(knownCurrentRunBytes, pendingMandatoryMetadataBytes(persistedManifest, reportResult));
    if (hasReport)
    {
        // The pictures of the earlier runs the masters are compared with are copied when the report is stored.
        const std::vector<QString> madeFolders(archivedGroupFolders.begin(),
                                               archivedGroupFolders.begin() +
                                                   static_cast<std::ptrdiff_t>(result.integrationGroupResults.size()));
        knownCurrentRunBytes =
            saturatingAddBytes(knownCurrentRunBytes,
                               earlierPicturesBound(sortedHistoryRunDirectories(historyDir, runDirPath), madeFolders));
    }
    const ArchiveRetentionPreflight retention =
        preflightArchiveRetention(historyDir, runDirPath, knownCurrentRunBytes, m_maxHistoryEntries, m_maxHistoryBytes);

    const auto write_group_summary = [&](const int groupIndex) {
        const auto& groupResult = result.integrationGroupResults[static_cast<size_t>(groupIndex)];
        const auto& reportGroupResult = reportResult.integrationGroupResults[static_cast<size_t>(groupIndex)];
        QJsonObject groupSummary;
        groupSummary[QStringLiteral("schemaVersion")] = 1;
        groupSummary[QStringLiteral("groupKey")] = groupResult.groupKey;
        groupSummary[QStringLiteral("outputPath")] = reportGroupResult.outputPath;
        groupSummary[QStringLiteral("outputFilename")] = groupResult.outputFilename;
        groupSummary[QStringLiteral("outputBytes")] = groupResult.outputBytes;
        groupSummary[QStringLiteral("previewPath")] =
            archivedPreviewByGroup.value(groupResult.groupKey, groupResult.previewPath);
        groupSummary[QStringLiteral("outputRetentionStatus")] =
            outputRetentionStatusToString(groupResult.outputRetentionStatus);
        if (!groupResult.outputRetentionReason.trimmed().isEmpty())
        {
            groupSummary[QStringLiteral("outputRetentionReason")] = groupResult.outputRetentionReason;
        }
        groupSummary[QStringLiteral("lightFrameCount")] = groupResult.lightFrameCount;
        groupSummary[QStringLiteral("registrationGroupKey")] = groupResult.registrationGroupKey;
        groupSummary[QStringLiteral("registrationInputPolicy")] = groupResult.registrationInputPolicy;
        groupSummary[QStringLiteral("masterSolution")] = masterSolutionOutcomeToString(groupResult.masterSolution);
        if (!groupResult.masterSolutionDetail.isEmpty())
        {
            groupSummary[QStringLiteral("masterSolutionDetail")] = groupResult.masterSolutionDetail;
        }
        groupSummary[QStringLiteral("durationMs")] = groupResult.durationMs;
        groupSummary[QStringLiteral("result")] =
            groupResult.success ? QStringLiteral("success") : QStringLiteral("failed");
        if (groupResult.finalizationTiming.captured)
        {
            groupSummary[QStringLiteral("finalizationTiming")] =
                stackGroupFinalizationTimingToJson(groupResult.finalizationTiming);
        }
        groupSummary[QStringLiteral("archivedFiles")] = archivedFilesByGroup.value(groupResult.groupKey);
        groupSummary[QStringLiteral("rejectionMaps")] = archivedRejectionMapsByGroup.value(groupResult.groupKey);
        groupSummary[QStringLiteral("qualityMaps")] = archivedQualityMapsByGroup.value(groupResult.groupKey);
        if (groupResult.qualityReport.valid)
        {
            groupSummary[QStringLiteral("qualityReport")] = stackingQualityReportToJson(groupResult.qualityReport);
        }
        if (!groupResult.droppedLights.empty())
        {
            groupSummary[QStringLiteral("droppedLights")] = droppedLightsToJson(groupResult.droppedLights);
        }

        const QString groupSummaryPath =
            QDir(runDir.filePath(QStringLiteral("groups/%1").arg(groupDirNames[static_cast<size_t>(groupIndex)])))
                .filePath(QStringLiteral("group_summary.json"));
        if (!writeAtomicFile(groupSummaryPath, QJsonDocument(groupSummary).toJson(QJsonDocument::Indented)))
        {
            LOG_WARNING("WorkflowArchiver: Failed to write group_summary.json for group {}",
                        groupResult.groupKey.toStdString());
        }
    };

    for (int groupIndex = 0; groupIndex < static_cast<int>(result.integrationGroupResults.size()); ++groupIndex)
    {
        auto& groupResult = result.integrationGroupResults[groupIndex];
        auto& reportGroupResult = reportResult.integrationGroupResults[groupIndex];
        const QFileInfo outputInfo(groupResult.outputPath);

        if (groupResult.success && outputInfo.exists() && outputInfo.isFile())
        {
            if (!retention.retainPayloads)
            {
                groupResult.outputRetentionStatus = OutputRetentionStatus::NotRetainedByPolicy;
                groupResult.outputRetentionReason = retention.reason;
            }
            else
            {
                const bool committed_snapshot_matches =
                    !groupResult.committedArtifact.has_value() ||
                    groupResult.committedArtifact->matchesFileSnapshot(outputInfo.absoluteFilePath());
                if (!committed_snapshot_matches)
                {
                    LOG_WARNING("WorkflowArchiver: current output no longer matches committed snapshot {}",
                                outputInfo.absoluteFilePath().toStdString());
                    groupResult.outputRetentionStatus = OutputRetentionStatus::NotRetainedByArchiveFailure;
                    groupResult.outputRetentionReason =
                        QStringLiteral("the scientific output changed after it was committed");
                }
                else
                {
                    const QString fileName = outputInfo.fileName();
                    const QString groupArchivePath = runDir.filePath(
                        QStringLiteral("groups/%1").arg(groupDirNames[static_cast<size_t>(groupIndex)]));
                    const QString destPath = QDir(groupArchivePath).filePath(fileName);
                    if (fileName.isEmpty() || !isPathContainedBy(groupArchivePath, destPath))
                    {
                        groupResult.outputRetentionStatus = OutputRetentionStatus::NotRetainedByArchiveFailure;
                        groupResult.outputRetentionReason = QStringLiteral("the scientific output path is unsafe");
                        continue;
                    }
                    const ImageIntegration::ArchiveFileTransportRequest transportRequest =
                        groupResult.committedArtifact.has_value()
                            ? ImageIntegration::ArchiveFileTransportRequest::forImmutableArtifact(
                                  *groupResult.committedArtifact, destPath, m_counters)
                            : ImageIntegration::ArchiveFileTransportRequest::forOrdinaryCopy(
                                  outputInfo.absoluteFilePath(), destPath, m_counters);
                    ImageIntegration::ArchiveFileTransportResult transportResult =
                        m_archiveTransport.transport(transportRequest);
                    archiveResult.transportDiagnostics.accumulate(transportResult.diagnostics);
                    archiveResult.payloadTransports.push_back(transportResult);
                    if (transportResult.succeeded())
                    {
                        const QFileInfo archivedInfo(destPath);
                        ++itemsArchived;
                        QJsonObject archivedObject;
                        archivedObject[QStringLiteral("filename")] = archivedInfo.fileName();
                        archivedObject[QStringLiteral("sizeBytes")] = archivedInfo.size();
                        if (groupResult.committedArtifact.has_value())
                        {
                            archivedObject[QStringLiteral("sha256")] = groupResult.committedArtifact->digest().sha256();
                            archivedObject[QStringLiteral("digestProvenance")] = QString::fromLatin1(
                                ImageIntegration::schema::kArtifactDigestProvenanceCurrentRunCommitted);
                            archivedObject[QStringLiteral("sourceModifiedAtUtc")] =
                                groupResult.committedArtifact->modificationTimestamp().toUTC().toString(
                                    Qt::ISODateWithMs);
                        }
                        archivedFilesByGroup.insert(groupResult.groupKey, QJsonArray{archivedObject});
                        reportGroupResult.outputPath =
                            QStringLiteral("groups/%1/%2")
                                .arg(groupDirNames[static_cast<size_t>(groupIndex)], fileName);
                        groupResult.outputRetentionStatus = OutputRetentionStatus::RetainedInHistory;
                        groupResult.outputRetentionReason.clear();
                    }
                    else
                    {
                        LOG_WARNING("WorkflowArchiver: Failed to transport current output {} to {}: {}",
                                    outputInfo.absoluteFilePath().toStdString(), destPath.toStdString(),
                                    transportResult.error.toStdString());
                        groupResult.outputRetentionStatus = OutputRetentionStatus::NotRetainedByArchiveFailure;
                        if (transportResult.outcome ==
                            ImageIntegration::ArchiveFileTransportOutcome::SourceChangedBeforeTransfer)
                        {
                            groupResult.outputRetentionReason =
                                QStringLiteral("the scientific output changed after it was committed");
                        }
                        else if (transportResult.outcome ==
                                 ImageIntegration::ArchiveFileTransportOutcome::SourceChangedDuringTransfer)
                        {
                            groupResult.outputRetentionReason =
                                QStringLiteral("the scientific output changed while it was being archived");
                        }
                        else
                        {
                            groupResult.outputRetentionReason =
                                QStringLiteral("the history copy could not be completed");
                        }
                    }
                }
            }
        }
        else if (groupResult.success && !groupResult.outputPath.trimmed().isEmpty())
        {
            groupResult.outputRetentionStatus = OutputRetentionStatus::NotRetainedByArchiveFailure;
            groupResult.outputRetentionReason =
                QStringLiteral("the scientific output file was unavailable for archiving");
        }

        reportGroupResult.outputRetentionStatus = groupResult.outputRetentionStatus;
        reportGroupResult.outputRetentionReason = groupResult.outputRetentionReason;

        write_group_summary(groupIndex);
    }

    const ArchiveStamp stamp = m_archiveStamp.has_value() ? *m_archiveStamp : hostArchiveStamp();
    const QDateTime archiveNow = stamp.archivedAt;
    // The local time is stated without its offset, which the archive states beside it.
    const QString archivedAtLocal = archiveNow.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz"));

    QJsonObject reportContext;
    reportContext[QStringLiteral("schemaVersion")] = 3;
    reportContext[QStringLiteral("executionId")] = manifest.executionId;
    reportContext[QStringLiteral("archivedAtUtc")] = archiveNow.toUTC().toString(Qt::ISODateWithMs);
    reportContext[QStringLiteral("archivedAtLocal")] = archivedAtLocal;
    reportContext[QStringLiteral("timeZoneId")] = stamp.timeZoneId;
    reportContext[QStringLiteral("utcOffsetSeconds")] = archiveNow.offsetFromUtc();
    if (!stamp.applicationVersion.isEmpty())
    {
        reportContext[QStringLiteral("appVersion")] = stamp.applicationVersion;
    }

    // 3. Write workflow_log.txt
    m_logPath = runDir.filePath(QStringLiteral("workflow_log.txt"));
    bool workflowLogPersisted = true;
    if (!reportResult.capturedLog.isEmpty())
    {
        const QByteArray workflowLogBytes = reportResult.capturedLog.toUtf8();
        workflowLogPersisted = m_workflowLogWriter ? m_workflowLogWriter(m_logPath, workflowLogBytes)
                                                   : writeAtomicFile(m_logPath, workflowLogBytes, QIODevice::Text);
        if (!workflowLogPersisted)
        {
            LOG_ERROR("WorkflowArchiver: Failed to write workflow_log.txt");
            abort_archive_transaction(QStringLiteral("Failed to write workflow_log.txt"));
            finish_archive_phase();
            return archiveResult;
        }
    }

    if (retention.retainPayloads && QFile::exists(metricsSnapshotSource))
    {
        const QString metricsSnapshotDest = runDir.filePath(QStringLiteral("registration_metrics.db"));
        ImageIntegration::ArchiveFileTransportResult transportResult =
            snapshotSqliteDatabase(metricsSnapshotSource, metricsSnapshotDest, m_counters);
        archiveResult.transportDiagnostics.accumulate(transportResult.diagnostics);
        archiveResult.payloadTransports.push_back(transportResult);
        if (!transportResult.succeeded())
        {
            LOG_WARNING("WorkflowArchiver: Failed to snapshot registration_metrics.db into archive '{}'",
                        metricsSnapshotDest.toStdString());
        }
    }

    const auto write_workflow_report = [&]() {
        if (!hasReport)
        {
            return;
        }

        finish_archive_phase();

        // The model is built once, here, of the run as the archive holds it - each master's thumbnail and maps, and
        // whether the history keeps a copy of it - with each master's file where the run wrote it, which is where
        // the person finds it. The metrics are the output folder's, as the run left them.
        using namespace ImageIntegration::Reporting;
        WorkflowExecutor::Result stated = reportResult;
        for (std::size_t index = 0; index < stated.integrationGroupResults.size(); ++index)
        {
            stated.integrationGroupResults[index].outputPath = result.integrationGroupResults[index].outputPath;
        }
        ProductionReportSources sources(m_session, committedRunMetricsDatabase(persistedManifest), m_counters);
        const ReportModelInputs inputs{
            .manifest = persistedManifest,
            .result = stated,
            .runDirectory = runDirPath,
            .context = reportContext,
            .counters = m_counters != nullptr ? std::optional<WorkflowExecutionCounters>(m_counters->snapshot())
                                              : std::nullopt};
        m_runDirectory = runDirPath;
        m_reportModel = std::make_shared<ReportModel>(buildReportModel(inputs, sources.sources()));
        // A report that cannot be written does not fail the commit: the run's outputs and its history are what the
        // commit is for, and the reason is logged.
        archiveResult.reportPath = storeKeptReport();

        archiveResult.reportMs += capturePhaseDuration();
    };

    // 3b. Write the report for integration executions only.
    write_workflow_report();

    // 4. Write workflow_manifest.json (v3)
    QJsonObject root;
    root["schemaVersion"] = 3;
    root["executionId"] = manifest.executionId;

    root["archivedAtUtc"] = archiveNow.toUTC().toString(Qt::ISODateWithMs);
    root["archivedAtLocal"] = archivedAtLocal;
    root["timeZoneId"] = stamp.timeZoneId;
    root["utcOffsetSeconds"] = archiveNow.offsetFromUtc();

    if (!stamp.applicationVersion.isEmpty() || qApp != nullptr)
    {
        root["appVersion"] = stamp.applicationVersion;
    }
    root["archiveReason"] = hasStagedFiles ? "overwrite" : "run";

    QString modeStr = "Full";
    if (manifest.executionMode == ExecutionMode::Incremental)
        modeStr = "Incremental";
    else if (manifest.executionMode == ExecutionMode::RegistrationOnly)
        modeStr = "RegistrationOnly";

    root["executionMode"] = modeStr;
    root["executionScope"] = manifest.executionScope;
    if (!persistedManifest.processLabel.trimmed().isEmpty())
    {
        root[QStringLiteral("processLabel")] = persistedManifest.processLabel.trimmed();
    }
    if (!persistedManifest.processingProfileId.trimmed().isEmpty() ||
        !persistedManifest.processingProfileLabel.trimmed().isEmpty())
    {
        QJsonObject processingProfile;
        if (!persistedManifest.processingProfileId.trimmed().isEmpty())
        {
            processingProfile.insert(QStringLiteral("id"), persistedManifest.processingProfileId.trimmed());
        }
        if (!persistedManifest.processingProfileLabel.trimmed().isEmpty())
        {
            processingProfile.insert(QStringLiteral("label"), persistedManifest.processingProfileLabel.trimmed());
        }
        root[QStringLiteral("processingProfile")] = processingProfile;
    }
    root["totalDurationMs"] = result.totalDurationMs;
    if (result.preparationTiming.hasMeasurements())
    {
        QJsonObject preparationTiming;
        preparationTiming[QStringLiteral("manifestBuildMs")] = result.preparationTiming.manifestBuildMs;
        preparationTiming[QStringLiteral("planAndValidationMs")] = result.preparationTiming.planAndValidationMs;
        root[QStringLiteral("preparationTiming")] = preparationTiming;
    }
    if (result.executorTiming.captured)
    {
        QJsonObject executorTiming;
        executorTiming[QStringLiteral("stageExecutionMs")] = result.executorTiming.stageExecutionMs;
        executorTiming[QStringLiteral("stateCommitMs")] = result.executorTiming.stateCommitMs;
        executorTiming[QStringLiteral("archiveMs")] = result.executorTiming.archiveMs;
        executorTiming[QStringLiteral("reportMs")] = result.executorTiming.reportMs;
        root[QStringLiteral("executorTiming")] = executorTiming;
    }
    if (m_counters != nullptr)
    {
        root[QStringLiteral("executionCounters")] = workflowExecutionCountersToJson(m_counters->snapshot());
    }
    else if (result.executionCounters.has_value())
    {
        root[QStringLiteral("executionCounters")] = workflowExecutionCountersToJson(result.executionCounters.value());
    }
    root["overallResult"] = result.success ? "success" : (result.cancelled ? "cancelled" : "failed");
    root["warningCount"] = result.warningCount;

    root["manifest"] = persistedManifest.toJson();

    QJsonArray stageResultsJson;
    for (const auto& sr : result.stageResults)
    {
        QJsonObject s;
        s["stage"] = sr.stageName;
        s["status"] = sr.status;
        s["durationMs"] = sr.durationMs;
        if (sr.groupsProcessed > 0)
            s["groupsProcessed"] = sr.groupsProcessed;
        stageResultsJson.append(s);
    }
    root["stageResults"] = stageResultsJson;

    const auto build_group_results_json = [&]() {
        QJsonArray groupResultsJson;
        for (const auto& gr : reportResult.integrationGroupResults)
        {
            QJsonObject g;
            g["groupKey"] = gr.groupKey;
            g["outputPath"] = gr.outputPath;
            g["outputFilename"] = gr.outputFilename;
            g["outputBytes"] = gr.outputBytes;
            g["previewPath"] = archivedPreviewByGroup.value(gr.groupKey, gr.previewPath);
            g["outputRetentionStatus"] = outputRetentionStatusToString(gr.outputRetentionStatus);
            if (!gr.outputRetentionReason.trimmed().isEmpty())
            {
                g["outputRetentionReason"] = gr.outputRetentionReason;
            }
            g["lightFrameCount"] = gr.lightFrameCount;
            g["registrationGroupKey"] = gr.registrationGroupKey;
            g["registrationInputPolicy"] = gr.registrationInputPolicy;
            g["masterSolution"] = masterSolutionOutcomeToString(gr.masterSolution);
            if (!gr.masterSolutionDetail.isEmpty())
            {
                g["masterSolutionDetail"] = gr.masterSolutionDetail;
            }
            g["durationMs"] = gr.durationMs;
            g["result"] = gr.success ? "success" : "failed";
            if (gr.finalizationTiming.captured)
            {
                g["finalizationTiming"] = stackGroupFinalizationTimingToJson(gr.finalizationTiming);
            }

            g["archivedFiles"] = archivedFilesByGroup.value(gr.groupKey);
            g["rejectionMaps"] = archivedRejectionMapsByGroup.value(gr.groupKey);
            g["qualityMaps"] = archivedQualityMapsByGroup.value(gr.groupKey);
            if (gr.qualityReport.valid)
            {
                g["qualityReport"] = stackingQualityReportToJson(gr.qualityReport);
            }
            if (!gr.droppedLights.empty())
            {
                g["droppedLights"] = droppedLightsToJson(gr.droppedLights);
            }

            groupResultsJson.append(g);
        }
        return groupResultsJson;
    };
    root["integrationGroupResults"] = build_group_results_json();
    if (!result.reusedIntegrationGroups.empty())
    {
        root[QStringLiteral("reusedIntegrationGroups")] = reusedIntegrationGroupsToJson(result.reusedIntegrationGroups);
    }

    QJsonArray calibrationResultsJson;
    for (const auto& calibrationResult : result.calibrationGroupResults)
    {
        QJsonObject c;
        c["type"] = calibrationResult.type;
        c["groupKey"] = calibrationResult.groupKey;
        c["masterPath"] = calibrationResult.masterPath;
        c["inputCount"] = calibrationResult.inputCount;
        c["result"] = calibrationResult.success ? "success" : "failed";
        calibrationResultsJson.append(c);
    }
    root["calibrationGroupResults"] = calibrationResultsJson;

    QJsonArray alignmentResultsJson;
    for (const auto& ar : result.alignmentGroupResults)
    {
        QJsonObject a;
        a[QStringLiteral("groupKey")] = ar.groupKey;
        a[QStringLiteral("totalAligned")] = ar.totalAligned;
        a[QStringLiteral("sipCorrectedCount")] = ar.sipCorrectedCount;
        a[QStringLiteral("caCorrectedCount")] = ar.caCorrectedCount;
        a[QStringLiteral("caPassthroughCount")] = ar.caPassthroughCount;
        alignmentResultsJson.append(a);
    }
    if (!alignmentResultsJson.isEmpty())
    {
        root[QStringLiteral("alignmentGroupResults")] = alignmentResultsJson;
    }

    QJsonArray warningsJson;
    for (const auto& w : result.warnings)
        warningsJson.append(w);
    root["warnings"] = warningsJson;

    QJsonArray errorsJson;
    for (const auto& e : result.errors)
        errorsJson.append(e);
    root["errors"] = errorsJson;

    const QString manifestPath = runDir.filePath(QStringLiteral("workflow_manifest.json"));
    if (writeAtomicFile(manifestPath, QJsonDocument(root).toJson(QJsonDocument::Indented)))
    {
        m_manifestPath = manifestPath;
        archiveResult.manifestPath = manifestPath;
        archiveResult.success = true;
    }
    else
    {
        abort_archive_transaction(QStringLiteral("Failed to write workflow_manifest.json"));
        finish_archive_phase();
        return archiveResult;
    }

    if (archiveResult.success)
    {
        for (int groupIndex = 0; groupIndex < static_cast<int>(result.integrationGroupResults.size()); ++groupIndex)
        {
            const auto& groupResult = result.integrationGroupResults[groupIndex];
            const QDir groupDir(
                runDir.filePath(QStringLiteral("groups/%1").arg(groupDirNames[static_cast<size_t>(groupIndex)])));
            for (const auto& staged : groupResult.stagedFiles)
            {
                if (!QFile::exists(staged.stagedPath))
                {
                    continue;
                }

                if (retention.retainPayloads)
                {
                    const QString fileName = QFileInfo(staged.originalPath).fileName();
                    const QString destination = groupDir.filePath(QStringLiteral("previous/%1").arg(fileName));
                    if (moveFile(staged.stagedPath, destination))
                    {
                        ++itemsArchived;
                    }
                    else
                    {
                        LOG_WARNING("WorkflowArchiver: Failed to move staged file {} to {}",
                                    staged.stagedPath.toStdString(), destination.toStdString());
                        (void)cleanupOrphanedStagedFiles(m_outputDir);
                        if (QFile::exists(staged.stagedPath))
                        {
                            LOG_ERROR("WorkflowArchiver: Failed to resolve staged file {} after archive move failure",
                                      staged.stagedPath.toStdString());
                        }
                    }
                    continue;
                }

                // A metadata-only archive deliberately drops displaced history payloads after its
                // manifest is durable; the successful current scientific output stays in place.
                if (!QFile::remove(staged.stagedPath))
                {
                    (void)cleanupOrphanedStagedFiles(m_outputDir);
                    if (QFile::exists(staged.stagedPath))
                    {
                        LOG_ERROR("WorkflowArchiver: Failed to resolve staged file {} for metadata-only archive",
                                  staged.stagedPath.toStdString());
                    }
                }
            }
        }
    }

    archiveResult.itemsArchived = itemsArchived;
    if (archiveResult.success)
        cleanupOldArchives(runDirPath);
    finish_archive_phase();
    return archiveResult;
}

QString WorkflowArchiver::storeKeptReport()
{
    if (!m_reportModel || m_runDirectory.isEmpty())
    {
        return {};
    }
    const ImageIntegration::Reporting::WorkflowReportStored stored =
        ImageIntegration::Reporting::storeWorkflowReport(*m_reportModel, m_runDirectory);
    for (const QString& picture : stored.picturesNotCopied)
    {
        LOG_WARNING("WorkflowArchiver: An earlier run's picture was not copied into the report: {}",
                    picture.toStdString());
    }
    for (const QString& fault : stored.faults)
    {
        LOG_WARNING("WorkflowArchiver: The report's document left something out: {}", fault.toStdString());
    }
    if (!stored.isStored())
    {
        LOG_WARNING("WorkflowArchiver: Failed to write the workflow report: {}", stored.error.toStdString());
        return {};
    }
    return stored.reportPath;
}

void WorkflowArchiver::finalizeTimingArtifacts(const WorkflowExecutor::Result& result)
{
    if (m_reportModel)
    {
        // What the run knows only now: its own phases and total, which include the archive and the report, its
        // diagnostics at the end, the engine's memory, and what its folder in the history holds.
        ImageIntegration::Reporting::restateRunCompletion(*m_reportModel, result);
        const DirectoryUse archive = directoryUse(m_runDirectory);
        ImageIntegration::Reporting::restateRunArchive(*m_reportModel, archive.bytes, archive.files);
        (void)storeKeptReport();
    }

    if (m_manifestPath.isEmpty())
    {
        return;
    }

    QFile archive_file(m_manifestPath);
    if (!archive_file.open(QIODevice::ReadOnly))
    {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(archive_file.readAll());
    archive_file.close();
    if (!document.isObject())
    {
        return;
    }

    QJsonObject root = document.object();
    root[QStringLiteral("totalDurationMs")] = result.totalDurationMs;
    if (result.executorTiming.captured)
    {
        QJsonObject executorTiming;
        executorTiming[QStringLiteral("stageExecutionMs")] = result.executorTiming.stageExecutionMs;
        executorTiming[QStringLiteral("stateCommitMs")] = result.executorTiming.stateCommitMs;
        executorTiming[QStringLiteral("archiveMs")] = result.executorTiming.archiveMs;
        executorTiming[QStringLiteral("reportMs")] = result.executorTiming.reportMs;
        root[QStringLiteral("executorTiming")] = executorTiming;
    }
    else
    {
        root.remove(QStringLiteral("executorTiming"));
    }
    if (result.executionCounters.has_value())
    {
        root[QStringLiteral("executionCounters")] = workflowExecutionCountersToJson(result.executionCounters.value());
    }
    else
    {
        root.remove(QStringLiteral("executionCounters"));
    }

    if (!writeAtomicFile(m_manifestPath, QJsonDocument(root).toJson(QJsonDocument::Indented)))
    {
        LOG_WARNING("WorkflowArchiver: Failed to finalize timing in workflow_manifest.json");
    }
}

void WorkflowArchiver::finalizeLogArtifact(const WorkflowExecutor::Result& result)
{
    if (m_logPath.isEmpty() || result.capturedLog.isEmpty())
    {
        return;
    }

    if (!writeAtomicFile(m_logPath, result.capturedLog.toUtf8(), QIODevice::Text))
    {
        LOG_WARNING("WorkflowArchiver: Failed to finalize workflow_log.txt");
    }

    // The report prints the log it was last given: the whole one, now that there is one.
    if (m_reportModel)
    {
        ImageIntegration::Reporting::restateRunLog(*m_reportModel, result.capturedLog);
        (void)storeKeptReport();
    }
}

void WorkflowArchiver::revertStagedFiles(const WorkflowExecutor::Result& result)
{
    for (const auto& groupResult : result.integrationGroupResults)
    {
        for (const auto& staged : groupResult.stagedFiles)
        {
            if (QFile::exists(staged.stagedPath))
            {
                if (QFile::exists(staged.originalPath))
                {
                    QFile::remove(staged.originalPath);
                }

                if (!QFile::rename(staged.stagedPath, staged.originalPath))
                {
                    if (copyWorkflowFile(staged.stagedPath, staged.originalPath, m_counters))
                    {
                        QFile::remove(staged.stagedPath);
                    }
                    else
                    {
                        LOG_ERROR("WorkflowArchiver: Failed to restore staged file {} to {}",
                                  staged.stagedPath.toStdString(), staged.originalPath.toStdString());
                    }
                }
            }
        }
    }
}

void WorkflowArchiver::cleanupOldArchives(const QString& protectedRunPath)
{
    const QString historyPath = QDir(m_outputDir).filePath("history");
    QDir historyDir(historyPath);
    if (!historyDir.exists())
        return;

    std::vector<QFileInfo> runDirs = sortedHistoryRunDirectories(historyDir);
    const QString normalizedProtectedPath = QDir::cleanPath(protectedRunPath);

    qint64 totalBytes = 0;
    for (const auto& info : runDirs)
    {
        totalBytes = saturatingAddBytes(totalBytes, directorySizeBytes(info.absoluteFilePath()));
    }

    auto shouldTrim = [&]() {
        return static_cast<int>(runDirs.size()) > m_maxHistoryEntries || totalBytes > m_maxHistoryBytes;
    };

    while (shouldTrim() && !runDirs.empty())
    {
        const auto oldestIt =
            std::find_if(runDirs.begin(), runDirs.end(), [&normalizedProtectedPath](const QFileInfo& run) {
                return normalizedProtectedPath.isEmpty() ||
                       QDir::cleanPath(run.absoluteFilePath()) != normalizedProtectedPath;
            });
        if (oldestIt == runDirs.end())
        {
            break;
        }

        const QFileInfo oldest = *oldestIt;
        const qint64 size = directorySizeBytes(oldest.absoluteFilePath());
        if (!QDir(oldest.absoluteFilePath()).removeRecursively())
        {
            LOG_WARNING("WorkflowArchiver: Failed to remove old history archive {}",
                        oldest.absoluteFilePath().toStdString());
            break;
        }
        totalBytes = (std::max)(static_cast<qint64>(0), totalBytes - size);
        runDirs.erase(oldestIt);
    }
}

/// The archiver behind the interface the executor commits a run through.
class WorkflowArchiverRunArchive final : public IWorkflowRunArchive
{
public:
    WorkflowArchiverRunArchive(const QString& outputDir, WorkflowExecutionCounterAccumulator* counters,
                               const an::consumer::EngineSession* session)
        : m_archiver(outputDir, counters)
    {
        m_archiver.setEngineSession(session);
    }

    CommitOutcome commit(const WorkflowManifest& manifest, WorkflowExecutor::Result& result,
                         const CapturePhaseDuration& capturePhase) override
    {
        const WorkflowArchiver::ArchiveResult archived = m_archiver.commit(manifest, result, capturePhase);
        CommitOutcome outcome;
        outcome.success = archived.success;
        outcome.reportPath = archived.reportPath;
        outcome.error = archived.error;
        outcome.archiveMs = archived.archiveMs;
        outcome.reportMs = archived.reportMs;
        return outcome;
    }

    void revertStagedFiles(const WorkflowExecutor::Result& result) override
    {
        m_archiver.revertStagedFiles(result);
    }

    void finalizeTimingArtifacts(const WorkflowExecutor::Result& result) override
    {
        m_archiver.finalizeTimingArtifacts(result);
    }

    void finalizeLogArtifact(const WorkflowExecutor::Result& result) override
    {
        m_archiver.finalizeLogArtifact(result);
    }

private:
    WorkflowArchiver m_archiver;
};

std::unique_ptr<IWorkflowRunArchive> makeWorkflowArchiverRunArchive(const QString& outputDir,
                                                                    WorkflowExecutionCounterAccumulator* counters,
                                                                    const an::consumer::EngineSession* session)
{
    return std::make_unique<WorkflowArchiverRunArchive>(outputDir, counters, session);
}
