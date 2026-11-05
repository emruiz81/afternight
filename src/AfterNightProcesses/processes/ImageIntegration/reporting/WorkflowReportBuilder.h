// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

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
/// One channel's figures the report prints for an output image, each read through the representation's display range
/// (`widgets/DisplayNormalization.h`) as the statistics window reads it: a location as a fraction of the range, a
/// spread as a share of it, and a variance or a mean of squares as a share of its square. The figures are over the
/// samples the image's validity admits that lie strictly inside the range - the clipped ones at either end are left
/// out.
struct WorkflowReportChannelStats
{
    /// How many samples the channel has: one per pixel.
    std::uint64_t samples = 0;

    /// How many samples the figures are over: those the image's validity admits, less the clipped ones.
    std::uint64_t unclippedSamples = 0;

    /// The smallest sample.
    double minimum = 0.0;

    /// The largest sample.
    double maximum = 0.0;

    /// The arithmetic mean.
    double mean = 0.0;

    /// The median.
    double median = 0.0;

    /// The population standard deviation, which the report prints as both its sigma and its standard deviation.
    double standardDeviation = 0.0;

    /// The mean absolute distance from the mean.
    double averageDeviation = 0.0;

    /// The population variance.
    double variance = 0.0;

    /// The median absolute deviation from the median, unscaled.
    double medianAbsoluteDeviation = 0.0;

    /// The background noise taken from each row's first differences.
    double backgroundNoise = 0.0;

    /// The biweight midvariance about the median.
    double biweightMidvariance = 0.0;

    /// The mean of the squared samples: the variance plus the square of the mean.
    double meanOfSquares = 0.0;

    /// The sum of the squared samples: the mean of squares times the samples the figures are over.
    double sumOfSquares = 0.0;

    /// The iterative k-sigma location, or nothing when the measurement could not state it.
    std::optional<double> ikssLocation;

    /// The iterative k-sigma scale, or nothing when the measurement could not state it.
    std::optional<double> ikssScale;
};

/// What the report measured of an output image it read.
struct WorkflowReportRawImageStats
{
    /// The image's width in pixels.
    size_t width = 0;

    /// The image's height in pixels.
    size_t height = 0;

    /// How many channels the image has.
    size_t channels = 0;

    /// Each channel's figures, in the image's channel order.
    std::vector<WorkflowReportChannelStats> channelStats;
};

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
