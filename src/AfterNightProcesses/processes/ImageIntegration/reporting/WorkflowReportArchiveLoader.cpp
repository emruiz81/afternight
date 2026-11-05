// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowReportArchiveLoader.h"

#include "processes/ImageIntegration/reporting/WorkflowReportBuilder.h"
#include "processes/ImageIntegration/workflow/ArtifactSchema.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/stages/StageResult.h"

#include <QJsonArray>
#include <QFile>
#include <QDir>

#include <cstdint>

namespace ImageIntegration::Reporting
{
namespace
{
[[nodiscard]] std::uint64_t archivedCounterValue(const QJsonObject& counters, const QString& key)
{
    bool ok = false;
    const qulonglong value = counters.value(key).toVariant().toULongLong(&ok);
    return ok ? static_cast<std::uint64_t>(value) : 0;
}
} // namespace

QString buildWorkflowReportMarkdownFromArchive(const QString& runDir, const QJsonObject& archiveRoot,
                                               const an::consumer::EngineSession* session)
{
    const QJsonObject embeddedManifest = archiveRoot.value(QStringLiteral("manifest")).toObject();
    if (embeddedManifest.isEmpty())
    {
        return QString();
    }

    WorkflowManifest manifest = WorkflowManifest::fromJson(embeddedManifest);

    WorkflowExecutor::Result result;
    result.totalDurationMs = archiveRoot.value(QStringLiteral("totalDurationMs")).toInt();
    result.warningCount = archiveRoot.value(QStringLiteral("warningCount")).toInt();
    const QJsonObject preparationTiming = archiveRoot.value(QStringLiteral("preparationTiming")).toObject();
    result.preparationTiming.captured = !preparationTiming.isEmpty();
    result.preparationTiming.manifestBuildMs = preparationTiming.value(QStringLiteral("manifestBuildMs")).toInteger();
    result.preparationTiming.planAndValidationMs =
        preparationTiming.value(QStringLiteral("planAndValidationMs")).toInteger();
    const QJsonObject executorTiming = archiveRoot.value(QStringLiteral("executorTiming")).toObject();
    result.executorTiming.captured = !executorTiming.isEmpty();
    result.executorTiming.stageExecutionMs = executorTiming.value(QStringLiteral("stageExecutionMs")).toInteger();
    result.executorTiming.stateCommitMs = executorTiming.value(QStringLiteral("stateCommitMs")).toInteger();
    result.executorTiming.archiveMs = executorTiming.value(QStringLiteral("archiveMs")).toInteger();
    result.executorTiming.reportMs = executorTiming.value(QStringLiteral("reportMs")).toInteger();
    const QJsonObject executionCounters = archiveRoot.value(QStringLiteral("executionCounters")).toObject();
    if (!executionCounters.isEmpty())
    {
        result.executionCounters.emplace(
            archivedCounterValue(executionCounters, QStringLiteral("bytesHashed")),
            archivedCounterValue(executionCounters, QStringLiteral("bytesCopied")),
            archivedCounterValue(executionCounters, QStringLiteral("sqliteStatementExecutions")),
            archivedCounterValue(executionCounters, QStringLiteral("sqliteTransactionsStarted")),
            archivedCounterValue(executionCounters, QStringLiteral("sqliteTransactionsCommitted")),
            archivedCounterValue(executionCounters, QStringLiteral("sqliteTransactionsRolledBack")),
            archivedCounterValue(executionCounters, QStringLiteral("inputDecodeOperations")),
            archivedCounterValue(executionCounters, QStringLiteral("scientificOutputEncodeCompletions")),
            archivedCounterValue(executionCounters, QStringLiteral("inputPassesStarted")),
            archivedCounterValue(executionCounters, QStringLiteral("tilePassesStarted")),
            archivedCounterValue(executionCounters, QStringLiteral("dumpBytesRead")),
            archivedCounterValue(executionCounters, QStringLiteral("dumpBytesWritten")),
            archivedCounterValue(executionCounters, QStringLiteral("pixmapBytesRead")),
            archivedCounterValue(executionCounters, QStringLiteral("pixmapBytesWritten")),
            archivedCounterValue(executionCounters, QStringLiteral("descriptorProviderHits")),
            archivedCounterValue(executionCounters, QStringLiteral("denseProviderHits")),
            archivedCounterValue(executionCounters, QStringLiteral("sipLazyProviderHits")),
            archivedCounterValue(executionCounters, QStringLiteral("polynomialLazyProviderHits")),
            archivedCounterValue(executionCounters, QStringLiteral("sipCoordinatesEvaluated")),
            archivedCounterValue(executionCounters, QStringLiteral("sharedSipPowerTableBuilds")),
            archivedCounterValue(executionCounters, QStringLiteral("explicitSipInverseEvaluations")),
            archivedCounterValue(executionCounters, QStringLiteral("sipNewtonFallbackIterations")),
            archivedCounterValue(executionCounters, QStringLiteral("polynomialCoordinatesEvaluated")),
            archivedCounterValue(executionCounters, QStringLiteral("sharedPolynomialPowerTableBuilds")),
            archivedCounterValue(executionCounters, QStringLiteral("lazyBlocksGenerated")),
            archivedCounterValue(executionCounters, QStringLiteral("lazyCoordinatePairsGenerated")),
            archivedCounterValue(executionCounters, QStringLiteral("peakLazyCoordinateBytes")),
            archivedCounterValue(executionCounters, QStringLiteral("denseFallbackCount")),
            archivedCounterValue(executionCounters, QStringLiteral("lazyMapCancellations")),
            archivedCounterValue(executionCounters, QStringLiteral("lazyMapFailures")),
            archivedCounterValue(executionCounters, QStringLiteral("stackLoaderRequests")),
            archivedCounterValue(executionCounters, QStringLiteral("roiSubsetReads")),
            archivedCounterValue(executionCounters, QStringLiteral("fullFrameFallbackReads")),
            archivedCounterValue(executionCounters, QStringLiteral("logicalDecodedPixelBytes")),
            archivedCounterValue(executionCounters, QStringLiteral("calibrationInvocations")),
            archivedCounterValue(executionCounters, QStringLiteral("calibratedSpillFilesCreated")),
            archivedCounterValue(executionCounters, QStringLiteral("calibratedSpillBytesWritten")),
            archivedCounterValue(executionCounters, QStringLiteral("calibratedSpillBytesRead")),
            archivedCounterValue(executionCounters, QStringLiteral("calibratedSpillRoiReads")),
            archivedCounterValue(executionCounters, QStringLiteral("calibratedSpillCleanupSuccesses")),
            archivedCounterValue(executionCounters, QStringLiteral("calibratedSpillCleanupFailures")));
    }

    const QString overallResult = archiveRoot.value(QStringLiteral("overallResult")).toString();
    result.success = (overallResult == QStringLiteral("success"));
    result.cancelled = (overallResult == QStringLiteral("cancelled"));

    const QJsonArray stageResults = archiveRoot.value(QStringLiteral("stageResults")).toArray();
    for (const auto& value : stageResults)
    {
        const QJsonObject obj = value.toObject();
        WorkflowExecutor::Result::StageTiming timing;
        timing.stageName = obj.value(QStringLiteral("stage")).toString();
        timing.status = obj.value(QStringLiteral("status")).toString();
        timing.durationMs = obj.value(QStringLiteral("durationMs")).toInt();
        timing.groupsProcessed = obj.value(QStringLiteral("groupsProcessed")).toInt();
        result.stageResults.push_back(std::move(timing));
    }

    const QJsonArray groupResults = archiveRoot.value(QStringLiteral("integrationGroupResults")).toArray();
    for (const auto& value : groupResults)
    {
        const QJsonObject obj = value.toObject();
        IntegrationGroupResult group;
        group.groupKey = obj.value(QStringLiteral("groupKey")).toString();
        group.outputPath = obj.value(QStringLiteral("outputPath")).toString();
        group.outputFilename = obj.value(QStringLiteral("outputFilename")).toString();
        group.outputBytes = obj.value(QStringLiteral("outputBytes")).toVariant().toLongLong();
        group.previewPath = obj.value(QStringLiteral("previewPath")).toString();
        group.outputRetentionStatus =
            outputRetentionStatusFromString(obj.value(QStringLiteral("outputRetentionStatus")).toString());
        group.outputRetentionReason = obj.value(QStringLiteral("outputRetentionReason")).toString();
        group.lightFrameCount = obj.value(QStringLiteral("lightFrameCount")).toInt();
        group.registrationGroupKey = obj.value(QStringLiteral("registrationGroupKey")).toString();
        group.registrationInputPolicy = obj.value(QStringLiteral("registrationInputPolicy")).toString();
        group.masterSolution = masterSolutionOutcomeFromString(obj.value(QStringLiteral("masterSolution")).toString());
        group.masterSolutionDetail = obj.value(QStringLiteral("masterSolutionDetail")).toString();
        group.durationMs = obj.value(QStringLiteral("durationMs")).toInt();
        group.success = (obj.value(QStringLiteral("result")).toString() == QStringLiteral("success"));
        group.finalizationTiming =
            stackGroupFinalizationTimingFromJson(obj.value(QStringLiteral("finalizationTiming")).toObject());
        group.qualityReport = stackingQualityReportFromJson(obj.value(QStringLiteral("qualityReport")).toObject());

        const QJsonArray rejectionMaps = obj.value(QStringLiteral("rejectionMaps")).toArray();
        for (const auto& mapValue : rejectionMaps)
        {
            const QJsonObject mapObj = mapValue.toObject();
            RejectionMapResult map;
            map.title = mapObj.value(QStringLiteral("title")).toString();
            map.path = mapObj.value(QStringLiteral("path")).toString();
            map.previewPath = mapObj.value(QStringLiteral("previewPath")).toString();
            group.rejectionMaps.push_back(std::move(map));
        }

        const QJsonArray qualityMaps = obj.value(QStringLiteral("qualityMaps")).toArray();
        for (const auto& mapValue : qualityMaps)
        {
            const QJsonObject mapObj = mapValue.toObject();
            QualityMapResult map;
            map.title = mapObj.value(QStringLiteral("title")).toString();
            map.path = mapObj.value(QStringLiteral("path")).toString();
            map.previewPath = mapObj.value(QStringLiteral("previewPath")).toString();
            group.qualityMaps.push_back(std::move(map));
        }

        result.integrationGroupResults.push_back(std::move(group));
    }

    const QJsonArray calibrationResults = archiveRoot.value(QStringLiteral("calibrationGroupResults")).toArray();
    for (const auto& value : calibrationResults)
    {
        const QJsonObject obj = value.toObject();
        WorkflowExecutor::Result::CalibrationGroupResult calibration;
        calibration.type = obj.value(QStringLiteral("type")).toString();
        calibration.groupKey = obj.value(QStringLiteral("groupKey")).toString();
        calibration.masterPath = obj.value(QStringLiteral("masterPath")).toString();
        calibration.inputCount = obj.value(QStringLiteral("inputCount")).toInt();
        calibration.success = (obj.value(QStringLiteral("result")).toString() == QStringLiteral("success"));
        result.calibrationGroupResults.push_back(std::move(calibration));
    }

    const QJsonArray alignmentResults = archiveRoot.value(QStringLiteral("alignmentGroupResults")).toArray();
    for (const auto& value : alignmentResults)
    {
        const QJsonObject obj = value.toObject();
        AlignmentGroupResult alignment;
        alignment.groupKey = obj.value(QStringLiteral("groupKey")).toString();
        alignment.totalAligned = obj.value(QStringLiteral("totalAligned")).toInt();
        alignment.sipCorrectedCount = obj.value(QStringLiteral("sipCorrectedCount")).toInt();
        alignment.caCorrectedCount = obj.value(QStringLiteral("caCorrectedCount")).toInt();
        alignment.caPassthroughCount = obj.value(QStringLiteral("caPassthroughCount")).toInt();
        result.alignmentGroupResults.push_back(std::move(alignment));
    }

    QFile workflowLog(QDir(runDir).filePath(QStringLiteral("workflow_log.txt")));
    if (workflowLog.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QByteArray logBytes = workflowLog.read(ImageIntegration::schema::kWorkflowLogMaxBytes + 1);
        if (workflowLog.error() != QFileDevice::NoError)
        {
            result.warnings.append(QStringLiteral("Execution log omitted because the archived log could not be read."));
        }
        else if (logBytes.size() > ImageIntegration::schema::kWorkflowLogMaxBytes)
        {
            result.warnings.append(
                QStringLiteral("Execution log omitted because it exceeds the %1-byte archive-report limit.")
                    .arg(ImageIntegration::schema::kWorkflowLogMaxBytes));
        }
        else
        {
            result.capturedLog = QString::fromUtf8(logBytes);
        }
    }

    const QJsonArray warnings = archiveRoot.value(QStringLiteral("warnings")).toArray();
    for (const auto& warningValue : warnings)
    {
        result.warnings.append(warningValue.toString());
    }

    const QJsonArray errors = archiveRoot.value(QStringLiteral("errors")).toArray();
    for (const auto& errorValue : errors)
    {
        result.errors.append(errorValue.toString());
    }

    WorkflowReportBuilder reportBuilder(manifest, result, runDir, archiveRoot, nullptr, session);
    return reportBuilder.build();
}

} // namespace ImageIntegration::Reporting
