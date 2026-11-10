// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "ReportModel.h"

#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace ImageIntegration::Reporting
{
/// One channel's figures of an output image, and what one read of the image measured: the report model's own.
using WorkflowReportChannelStats = ReportChannelStatistics;
using WorkflowReportRawImageStats = ReportImageStatistics;

class AFTERNIGHTPROCESSES_API WorkflowReportBuilder
{
public:
    /// A builder of one run's report.
    ///
    /// @param manifest the run's manifest, borrowed for the builder's life.
    /// @param result the run's result, borrowed for the builder's life.
    /// @param reportBaseDir the directory relative paths in the report resolve against.
    /// @param reportContext the archive's context: the version, the execution and the times it states.
    /// @param counters the run's diagnostics, if collected.
    /// @param session the session the report reads its output images and headers in, borrowed for the builder's life;
    ///        without one the report states no figure that needs a file read and no photometric weight.
    explicit WorkflowReportBuilder(const WorkflowManifest& manifest, const WorkflowExecutor::Result& result,
                                   QString reportBaseDir = QString(), QJsonObject reportContext = QJsonObject(),
                                   WorkflowExecutionCounterAccumulator* counters = nullptr,
                                   const an::consumer::EngineSession* session = nullptr);

    [[nodiscard]] QString build() const;
    [[nodiscard]] static QString finalizeTimingPlaceholders(QString report, const WorkflowExecutor::Result& result);

private:
    const WorkflowManifest& m_manifest;
    const WorkflowExecutor::Result& m_result;
    QString m_reportBaseDir;
    QJsonObject m_reportContext;
    WorkflowExecutionCounterAccumulator* m_counters = nullptr;
    const an::consumer::EngineSession* m_session = nullptr;
    mutable QHash<QString, WorkflowReportRawImageStats> m_rawImageStatsCache;
    mutable QSet<QString> m_rawImageStatsMisses;

    void appendMasthead(QString& md, const QDateTime& now) const;
    void appendReportProvenance(QString& md, const QDateTime& now) const;
    void appendSessionNotes(QString& md) const;
    void appendIndex(QString& md) const;
    void appendPrintFooterContext(QString& md, const QDateTime& now) const;
    void appendQualityGate(QString& md) const;
    void appendFrameQualityWeighting(QString& md) const;
    void appendTargetSummaries(QString& md) const;
    void appendReproducibilityFingerprint(QString& md) const;
    void appendComparedToPreviousRunDeltas(QString& md) const;
    void appendFailureDiagnostics(QString& md) const;
    void appendMetadataCompletenessScore(QString& md) const;

    void appendExecutiveSummary(QString& md) const;
    void appendSummary(QString& md) const;
    void appendAcquisitionSummary(QString& md) const;
    void appendEquipmentUsed(QString& md) const;
    void appendRunFocus(QString& md) const;
    void appendWorkflowTiming(QString& md) const;
    void appendWorkflowCounters(QString& md) const;
    void appendPipelineStages(QString& md) const;
    void appendStackedMasters(QString& md) const;
    void appendCalibrationSummary(QString& md) const;
    void appendRegistrationSummary(QString& md) const;
    void appendCometTrackingDiagnostics(QString& md, const QString& metricsDbPath) const;
    void appendSpccHandoffDiagnostics(QString& md) const;
    void appendFullProcessingParameters(QString& md) const;
    void appendAutoDecisions(QString& md) const;
    void appendInputFiles(QString& md) const;
    void appendWarnings(QString& md) const;
    void appendTechnicalAppendix(QString& md) const;
    void appendDetailedInputFileLists(QString& md) const;
    void appendExecutionLog(QString& md) const;

    [[nodiscard]] bool didStageRun(const QString& stageName) const;
    [[nodiscard]] int inferGroupCountForStage(const QString& stageName) const;
    [[nodiscard]] QString resolvePathForRead(const QString& candidatePath) const;
    [[nodiscard]] QString formatDuration(qint64 ms) const;
    [[nodiscard]] QString formatFileSize(qint64 bytes) const;
    [[nodiscard]] QString computeReproducibilityFingerprint(QString* manifestSettingsFingerprint,
                                                            QString* inputFingerprint) const;
    [[nodiscard]] std::optional<WorkflowReportRawImageStats> analyzeRawImageCached(const QString& path) const;
};
} // namespace ImageIntegration::Reporting
