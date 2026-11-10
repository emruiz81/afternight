// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportSources.h"

#include "facts/ReportFacts.h"

#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/core/RegistrationMetricsStateLoader.h"

#include "controls/ImageStatisticsReport.h"
#include "controls/ImageViewFiles.h"
#include "widgets/DisplayNormalization.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Statistics/AdvancedStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSysInfo>
#include <QTimeZone>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <stop_token>
#include <string>
#include <type_traits>
#include <utility>

namespace ImageIntegration::Reporting
{
namespace
{
/// The samples of an image the report's figures are over, where a range can state them: those the image's validity
/// admits that lie strictly inside its display range, the clipped ones at either end left out.
struct UnclippedSelection
{
    /// The selection the engine's statistics read.
    an::StatisticsSelection selection{};

    /// Whether the selection admits no sample at all.
    bool admitsNothing = false;
};

/// The samples strictly inside a representation's display range, joined with the value rule the image states, as the
/// statistics window selects them: an integer's ends are whole codes, so half a code inside them separates them, and a
/// float's are the nearest doubles inside zero and one.
///
/// @param sampleType the image's representation.
/// @param validity the image's validity.
/// @return the selection; or nothing for a rule a range cannot state - an explicit mask, or zero as no data inside a
///         signed range - whose figures are the statistics window's own.
[[nodiscard]] std::optional<UnclippedSelection> unclippedSelection(const an::SampleType sampleType,
                                                                   const an::ValidityDescriptor& validity)
{
    const an::ui::DisplayNormalization normalization = an::ui::displayNormalization(sampleType);
    double lowest = normalization.integer ? normalization.lowest + 0.5 : std::nextafter(0.0, 1.0);
    double highest =
        normalization.integer ? normalization.lowest + normalization.range - 0.5 : std::nextafter(1.0, 0.0);

    UnclippedSelection selected;
    switch (validity.rule())
    {
    case an::ValidityRule::AllValid:
    case an::ValidityRule::FiniteOnly:
        // A range admits no sample that is not a finite number.
        break;
    case an::ValidityRule::DomainRange:
        lowest = (std::max)(lowest, validity.lowestValid().value_or(lowest));
        highest = (std::min)(highest, validity.highestValid().value_or(highest));
        if (lowest > highest)
        {
            selected.admitsNothing = true;
            return selected;
        }
        break;
    case an::ValidityRule::ZeroIsNoData:
        // Zero is at the bottom of an unsigned or floating range, outside the interval; a signed one holds it inside.
        if (normalization.lowest < 0.0)
        {
            return std::nullopt;
        }
        break;
    case an::ValidityRule::ExplicitMask:
        return std::nullopt;
    }

    const std::optional<an::ValidityDescriptor> range = an::ValidityDescriptor::create(an::ValidityDescriptorRequest{
        .rule = an::ValidityRule::DomainRange, .lowestValid = lowest, .highestValid = highest});
    if (!range.has_value())
    {
        return std::nullopt;
    }
    selected.selection.validity = *range;
    return selected;
}

/// Completes a channel's figures with the two square figures, which follow from its mean and variance.
void completeSquareFigures(RawChannelStats& figures)
{
    figures.meanOfSquares = figures.variance + figures.mean * figures.mean;
    figures.sumOfSquares = figures.meanOfSquares * static_cast<double>(figures.unclippedSamples);
}

/// One channel's figures from the engine's statistics, read in the representation's own numbers.
[[nodiscard]] RawChannelStats channelStatsFrom(const an::ui::DisplayNormalization& normalization,
                                               const std::uint64_t samples, const an::BasicStatistics& basic,
                                               const an::AdvancedStatistics& advanced)
{
    const double range = normalization.range;
    RawChannelStats figures;
    figures.samples = samples;
    figures.unclippedSamples = basic.selection.valid;
    figures.minimum = an::ui::displayFraction(normalization, basic.minimum);
    figures.maximum = an::ui::displayFraction(normalization, basic.maximum);
    figures.mean = an::ui::displayFraction(normalization, basic.mean);
    figures.median = an::ui::displayFraction(normalization, advanced.median);
    figures.standardDeviation = basic.populationStandardDeviation / range;
    figures.averageDeviation = advanced.averageAbsoluteDeviation / range;
    figures.variance = basic.populationVariance / (range * range);
    figures.medianAbsoluteDeviation = advanced.medianAbsoluteDeviation / range;
    figures.backgroundNoise = advanced.backgroundNoise / range;
    figures.biweightMidvariance = advanced.biweightMidvariance / (range * range);
    figures.ikssLocation = an::ui::displayFraction(normalization, advanced.iterativeKsigmaLocation);
    figures.ikssScale = advanced.iterativeKsigmaScale / range;
    completeSquareFigures(figures);
    return figures;
}

/// One channel's figures as the statistics window computes them, which state no iterative k-sigma estimator.
[[nodiscard]] RawChannelStats channelStatsFrom(const an::ui::ImageStatisticsChannel& channel)
{
    RawChannelStats figures;
    figures.samples = channel.pixels;
    figures.unclippedSamples = channel.count;
    figures.minimum = channel.minimum;
    figures.maximum = channel.maximum;
    figures.mean = channel.mean;
    figures.median = channel.median;
    figures.standardDeviation = channel.standardDeviation;
    figures.averageDeviation = channel.averageDeviation;
    figures.variance = channel.variance;
    figures.medianAbsoluteDeviation = channel.medianAbsoluteDeviation;
    figures.backgroundNoise = channel.backgroundNoise;
    figures.biweightMidvariance = channel.biweightMidvariance;
    completeSquareFigures(figures);
    return figures;
}

/// How a measurement of an image read under its lease ended.
enum class RawMeasurement
{
    /// Every channel was measured.
    Measured,

    /// The image's validity is one a range cannot state; the statistics window's figures stand in.
    NeedsWindowFigures,

    /// The image holds nothing or the engine refused.
    Failed
};

/// Measures every channel of an image read under its lease, over its unclipped samples, into `stats`.
[[nodiscard]] RawMeasurement measureRawChannels(const an::consumer::OperationScope& scope,
                                                const an::consumer::AssetReadLease& lease, RawImageStats& stats)
{
    return lease.visit([&](const auto& typed) -> RawMeasurement {
        using ImageT = std::decay_t<decltype(typed)>;
        const auto view = typed.view();
        if (!view.has_value())
        {
            return RawMeasurement::Failed;
        }
        const an::ImageLayout layout = view->layout();
        stats.width = static_cast<size_t>(layout.extent().width());
        stats.height = static_cast<size_t>(layout.extent().height());
        stats.channels = static_cast<size_t>(layout.extent().channelCount());

        const std::optional<UnclippedSelection> selected =
            unclippedSelection(ImageT::kSampleType, typed.metadata().semantics().validity());
        if (!selected.has_value())
        {
            return RawMeasurement::NeedsWindowFigures;
        }

        const an::ui::DisplayNormalization normalization = an::ui::displayNormalization(ImageT::kSampleType);
        const std::uint64_t samples = layout.extent().pixelCount();
        const std::uint32_t channelCount = layout.extent().channelCount();
        stats.channelStats.clear();
        stats.channelStats.reserve(channelCount);
        for (std::uint32_t channel = 0u; channel < channelCount; ++channel)
        {
            RawChannelStats empty;
            empty.samples = samples;
            empty.ikssLocation = 0.0;
            empty.ikssScale = 0.0;
            if (selected->admitsNothing)
            {
                stats.channelStats.push_back(empty);
                continue;
            }
            const an::Result<an::BasicStatistics> basic = an::computeImageBasicStatistics(
                scope.execution(), ImageT::kSampleType, view->data(), layout, channel, selected->selection);
            if (!basic.hasValue())
            {
                if (basic.status().code() == an::ErrorCode::EmptySelection)
                {
                    stats.channelStats.push_back(empty);
                    continue;
                }
                LOG_WARNING("WorkflowReportBuilder: channel {} could not be measured: {}", channel,
                            basic.status().message());
                return RawMeasurement::Failed;
            }
            const an::Result<an::AdvancedStatistics> advanced = an::computeImageAdvancedStatistics(
                scope.execution(), ImageT::kSampleType, view->data(), layout, channel, selected->selection);
            if (!advanced.hasValue())
            {
                LOG_WARNING("WorkflowReportBuilder: channel {} could not be measured: {}", channel,
                            advanced.status().message());
                return RawMeasurement::Failed;
            }
            stats.channelStats.push_back(channelStatsFrom(normalization, samples, *basic, *advanced));
        }
        return RawMeasurement::Measured;
    });
}

[[nodiscard]] QList<an::ui::CometTrackingRunRecord>
queryCometTrackingRunsForReport(const QString& metricsDbPath, const WorkflowManifest& manifest,
                                WorkflowExecutionCounterAccumulator* counters)
{
    QList<an::ui::CometTrackingRunRecord> runs;
    if (metricsDbPath.isEmpty() || !QFileInfo::exists(metricsDbPath))
    {
        return runs;
    }

    QSet<QString> groupKeys;
    for (const auto& group : manifest.registrationGroups)
    {
        const QString groupKey = group.groupKey.toString().trimmed();
        if (!groupKey.isEmpty())
        {
            groupKeys.insert(groupKey);
        }
    }
    if (groupKeys.isEmpty())
    {
        return runs;
    }

    const QString connectionName =
        QStringLiteral("WorkflowReportCometRuns_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(metricsDbPath);
        if (db.open())
        {
            QStringList sortedGroupKeys = groupKeys.values();
            std::sort(sortedGroupKeys.begin(), sortedGroupKeys.end());
            for (const QString& groupKey : sortedGroupKeys)
            {
                QSqlQuery query(db);
                query.prepare(QStringLiteral(
                    "SELECT comet_run_id, group_key, settings_version, comet_context_id, detection_hash, "
                    "registration_hash, light_files_hash, calibration_hash, resolved_reference_frame_id, "
                    "reference_epoch, mode, status, created_at, updated_at "
                    "FROM comet_tracking_runs "
                    "WHERE group_key = :group_key "
                    "ORDER BY updated_at DESC, comet_run_id ASC"));
                query.bindValue(QStringLiteral(":group_key"), groupKey);
                if (!core::executeSqliteStatement(query, counters))
                {
                    continue;
                }

                while (query.next())
                {
                    an::ui::CometTrackingRunRecord run;
                    run.cometRunId = query.value(0).toString();
                    run.groupKey = query.value(1).toString();
                    run.settingsVersion = query.value(2).toInt();
                    run.cometContextId = query.value(3).toString();
                    run.detectionHash = query.value(4).toString();
                    run.registrationHash = query.value(5).toString();
                    run.lightFilesHash = query.value(6).toString();
                    run.calibrationHash = query.value(7).toString();
                    run.resolvedReferenceFrameId = query.value(8).toString();
                    run.referenceEpoch = query.value(9).toDouble();
                    run.mode = query.value(10).toString();
                    run.status = query.value(11).toString();
                    run.createdAt = query.value(12).toLongLong();
                    run.updatedAt = query.value(13).toLongLong();
                    if (!run.cometRunId.trimmed().isEmpty())
                    {
                        runs.append(std::move(run));
                    }
                }
            }
            db.close();
        }
    }

    QSqlDatabase::removeDatabase(connectionName);
    return runs;
}

[[nodiscard]] std::optional<int> querySpccHandoffSettingsVersion(QSqlDatabase& db, const QString& groupKey,
                                                                 WorkflowExecutionCounterAccumulator* counters)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT MAX(settings_version) FROM registration_matched_sources "
                                 "WHERE group_key = :group_key"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    if (!core::executeSqliteStatement(query, counters) || !query.next())
    {
        return std::nullopt;
    }

    bool ok = false;
    const int version = query.value(0).toInt(&ok);
    if (!ok || version < 0)
    {
        return std::nullopt;
    }
    return version;
}
} // namespace

/// Reads an output image in the session and measures each channel over its unclipped samples.
///
/// The figures do not depend on the width: the engine's reductions split their work the same way at any width.
///
/// @param session the session the image is read and measured in, or null.
/// @param path the image.
/// @param width the most threads the measurement may use - the run's, `core::parallelCoreWidth` of its setting.
/// @return the figures; nothing without a session, for a file that is not there or cannot be read, or when the engine
///         refused a measurement.
[[nodiscard]] std::optional<RawImageStats> analyzeRawImage(const an::consumer::EngineSession* session,
                                                           const QString& path, std::uint32_t width)
{
    if (session == nullptr || path.trimmed().isEmpty())
    {
        return std::nullopt;
    }

    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile())
    {
        return std::nullopt;
    }

    an::Result<an::ui::ViewReadResult> read =
        an::ui::readViewImage(*session, an::ui::ViewReadRequest{.path = path}, std::stop_token{}, nullptr);
    if (!read.hasValue())
    {
        LOG_WARNING("WorkflowReportBuilder: '{}' could not be read: {}", path.toStdString(), read.status().message());
        return std::nullopt;
    }
    // The read lease is taken before the admission, as every reader of an asset takes it.
    an::Result<an::consumer::AssetReadLease> lease = read->asset.read(std::stop_token{});
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::OperationScope> scope = session->admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = width});
    if (!scope.hasValue())
    {
        LOG_WARNING("WorkflowReportBuilder: '{}' was not measured: {}", path.toStdString(), scope.status().message());
        return std::nullopt;
    }

    RawImageStats rawStats;
    const RawMeasurement measured = measureRawChannels(*scope, *lease, rawStats);
    if (measured == RawMeasurement::Failed)
    {
        return std::nullopt;
    }
    if (measured == RawMeasurement::NeedsWindowFigures)
    {
        const an::Result<an::ui::ImageStatisticsReport> report =
            an::ui::computeImageStatisticsReport(*scope, *lease, true);
        if (!report.hasValue())
        {
            LOG_WARNING("WorkflowReportBuilder: '{}' could not be measured: {}", path.toStdString(),
                        report.status().message());
            return std::nullopt;
        }
        rawStats.channelStats.clear();
        rawStats.channelStats.reserve(report->channels.size());
        for (const an::ui::ImageStatisticsChannel& channel : report->channels)
        {
            rawStats.channelStats.push_back(channelStatsFrom(channel));
        }
    }
    if (rawStats.channelStats.empty())
    {
        return std::nullopt;
    }
    return rawStats;
}

/// Reads a file's header in the session.
///
/// @param session the session the probe is admitted in, or null.
/// @param path the file.
/// @return the header; nothing without a session, for a file that cannot be probed, or when no operation was admitted.
[[nodiscard]] std::optional<ReportHeader> probeReportHeader(const an::consumer::EngineSession* session,
                                                            const QString& path)
{
    if (session == nullptr || path.trimmed().isEmpty())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::OperationScope> scope = session->admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
    if (!scope.hasValue())
    {
        return std::nullopt;
    }
    const std::string utf8Path = path.toUtf8().toStdString();
    an::Result<an::CodecImageInfo> probed = an::probeImage(
        scope->engine(), scope->execution(), an::CodecReadRequest{.path = utf8Path, .validatePayload = false});
    if (!probed.hasValue())
    {
        return std::nullopt;
    }
    return ReportHeader{.width = probed->descriptor.extent().width(),
                        .height = probed->descriptor.extent().height(),
                        .metadata = std::move(probed->metadata)};
}

[[nodiscard]] std::optional<QString> transformKindFromJson(const QString& json)
{
    const QByteArray bytes = json.trimmed().toUtf8();
    if (bytes.isEmpty())
    {
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        return std::nullopt;
    }

    const QString kind = doc.object().value(QStringLiteral("kind")).toString().trimmed();
    if (kind.isEmpty())
    {
        return std::nullopt;
    }
    return kind;
}

[[nodiscard]] std::optional<int> queryRegistrationSettingsVersion(QSqlDatabase& db, const QString& groupKey,
                                                                  WorkflowExecutionCounterAccumulator* counters)
{
    QSqlQuery query(db);
    query.prepare(
        QStringLiteral("SELECT MAX(settings_version) FROM registration_metrics WHERE group_key = :group_key"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    if (!core::executeSqliteStatement(query, counters) || !query.next())
    {
        return std::nullopt;
    }

    bool ok = false;
    const int version = query.value(0).toInt(&ok);
    if (!ok || version < 0)
    {
        return std::nullopt;
    }
    return version;
}

[[nodiscard]] QHash<QString, RegistrationMetricRow>
loadRegistrationMetricsByFrame(QSqlDatabase& db, const QString& groupKey, int settingsVersion,
                               WorkflowExecutionCounterAccumulator* counters)
{
    QHash<QString, RegistrationMetricRow> byFrame;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT frame_id, stars, fwhm, snr, eccentricity, "
        "photometric_zero_point, zero_point_scatter, astrometric_rms_pixels, astrometric_rms_arcsec, "
        "limiting_magnitude, catalog_matched_stars, "
        "succeeded, transform_json "
        "FROM registration_metrics WHERE group_key = :group_key AND settings_version = :settings_version"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":settings_version"), settingsVersion);
    if (!core::executeSqliteStatement(query, counters))
    {
        return byFrame;
    }

    while (query.next())
    {
        const QString frameId = FrameId(query.value(0).toString()).canonicalPath();
        if (frameId.isEmpty())
        {
            continue;
        }

        RegistrationMetricRow row;
        row.stars = query.value(1).toInt();
        row.fwhm = query.value(2).toDouble();
        row.snr = query.value(3).toDouble();
        row.eccentricity = query.value(4).toDouble();
        row.photometricZeroPoint = query.value(5).toDouble();
        row.zeroPointScatter = query.value(6).toDouble();
        row.astrometricRmsPixels = query.value(7).toDouble();
        row.astrometricRmsArcsec = query.value(8).toDouble();
        row.limitingMagnitude = query.value(9).toDouble();
        row.catalogMatchedStars = query.value(10).toInt();
        row.succeeded = query.value(11).toInt() == 1;
        const auto transformKind = transformKindFromJson(query.value(12).toString());
        if (transformKind.has_value())
        {
            row.transformKind = transformKind->trimmed().toLower();
        }
        byFrame.insert(frameId, row);
    }

    return byFrame;
}

[[nodiscard]] QString registrationMetricsDatabasePath(const WorkflowManifest& manifest, const QString& reportBaseDir)
{
    QString outputDir = manifest.outputDirectory.trimmed();
    const QString reportDir = reportBaseDir.trimmed();
    const QString archiveMetricsPath =
        reportDir.isEmpty() ? QString() : QDir(reportDir).filePath(QStringLiteral("registration_metrics.db"));
    if (outputDir.isEmpty())
    {
        return archiveMetricsPath;
    }

    const QString outputMetricsPath = QDir(outputDir).filePath(QStringLiteral("registration_metrics.db"));
    if (QFileInfo::exists(outputMetricsPath) || archiveMetricsPath.isEmpty() || !QFileInfo::exists(archiveMetricsPath))
    {
        return outputMetricsPath;
    }

    return archiveMetricsPath;
}

[[nodiscard]] QHash<QString, RegistrationMetricRow>
loadRegistrationMetricsForGroup(const QString& metricsDbPath, const QString& groupKey,
                                WorkflowExecutionCounterAccumulator* counters)
{
    QHash<QString, RegistrationMetricRow> rows;
    if (metricsDbPath.isEmpty() || !QFileInfo::exists(metricsDbPath))
    {
        return rows;
    }

    const QString connectionName =
        QStringLiteral("WorkflowReportAutoMetrics_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(metricsDbPath);
        if (db.open())
        {
            const auto settingsVersion = queryRegistrationSettingsVersion(db, groupKey, counters);
            if (settingsVersion.has_value())
            {
                rows = loadRegistrationMetricsByFrame(db, groupKey, *settingsVersion, counters);
            }
            db.close();
        }
    }

    QSqlDatabase::removeDatabase(connectionName);
    return rows;
}

/// Each frame's background noise and scales as the registration recorded them, by canonical path, for a frame whose
/// registration succeeded: what the stack's noise weighting reads. Nothing where the records hold no noise.
[[nodiscard]] QHash<QString, FrameNoiseStatistics>
loadRegistrationNoiseByFrame(QSqlDatabase& db, const QString& groupKey, int settingsVersion,
                             WorkflowExecutionCounterAccumulator* counters)
{
    QHash<QString, FrameNoiseStatistics> byFrame;
    QSqlQuery query(db);
    query.prepare(
        QStringLiteral("SELECT frame_id, background_noise, global_mad, global_scale FROM registration_metrics "
                       "WHERE group_key = :group_key AND settings_version = :settings_version AND succeeded = 1"));
    query.bindValue(QStringLiteral(":group_key"), groupKey);
    query.bindValue(QStringLiteral(":settings_version"), settingsVersion);
    if (!core::executeSqliteStatement(query, counters))
    {
        return byFrame;
    }
    while (query.next())
    {
        const QString frameId = FrameId(query.value(0).toString()).canonicalPath();
        if (frameId.isEmpty())
        {
            continue;
        }
        FrameNoiseStatistics noise;
        noise.background_noise = query.value(1).toDouble();
        noise.global_mad = query.value(2).toDouble();
        noise.global_scale = query.value(3).toDouble();
        byFrame.insert(frameId, noise);
    }
    return byFrame;
}

[[nodiscard]] QList<CometReportRun> loadCometReportRuns(const QString& metricsDbPath, const WorkflowManifest& manifest,
                                                        WorkflowExecutionCounterAccumulator* counters)
{
    QList<CometReportRun> reportRuns;
    const QList<an::ui::CometTrackingRunRecord> runs =
        queryCometTrackingRunsForReport(metricsDbPath, manifest, counters);
    if (runs.isEmpty())
    {
        return reportRuns;
    }

    an::ui::RegistrationMetricsStateLoader loader(metricsDbPath, QStringLiteral("WorkflowReportCometSidecar"),
                                                  counters);
    QString error;
    if (!loader.open(&error))
    {
        return reportRuns;
    }

    for (const auto& run : runs)
    {
        CometReportRun reportRun;
        reportRun.run = run;
        reportRun.summary = loader.loadCometTrackingRunSummary(run.cometRunId, nullptr);
        (void)loader.loadCometTrackingPoints(run.cometRunId, reportRun.points, nullptr);
        reportRun.cometAlignedProduct =
            loader.loadCometTransformRoleProduct(run.cometRunId, QStringLiteral("comet_aligned"), nullptr);
        reportRuns.append(std::move(reportRun));
    }

    return reportRuns;
}

[[nodiscard]] QList<SpccHandoffGroupSummary> loadSpccHandoffSummaries(QSqlDatabase& db,
                                                                      const WorkflowManifest& manifest,
                                                                      WorkflowExecutionCounterAccumulator* counters)
{
    QList<SpccHandoffGroupSummary> summaries;
    for (const auto& group : manifest.registrationGroups)
    {
        const QString groupKey = group.groupKey.toString().trimmed();
        if (groupKey.isEmpty())
        {
            continue;
        }

        const auto settingsVersion = querySpccHandoffSettingsVersion(db, groupKey, counters);
        if (!settingsVersion.has_value())
        {
            continue;
        }

        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT frame_id, gaia_source_id, astrometric_catalog_manifest_id, "
                                     "spcc_catalog_manifest_id, match_separation_arcsec, residual_px, lookup_status "
                                     "FROM registration_matched_sources "
                                     "WHERE group_key = :group_key AND settings_version = :settings_version "
                                     "ORDER BY frame_id ASC, gaia_source_id ASC"));
        query.bindValue(QStringLiteral(":group_key"), groupKey);
        query.bindValue(QStringLiteral(":settings_version"), *settingsVersion);
        if (!core::executeSqliteStatement(query, counters))
        {
            continue;
        }

        SpccHandoffGroupSummary summary;
        summary.groupKey = groupKey;
        summary.groupName = friendlyGroupKey(groupKey);
        summary.settingsVersion = *settingsVersion;

        QSet<QString> frames;
        QSet<QString> sourceIds;
        QSet<QString> astrometricManifests;
        QSet<QString> spccManifests;
        while (query.next())
        {
            summary.referenceCount++;
            const QString frameId = query.value(0).toString().trimmed();
            const QString sourceId = query.value(1).toString().trimmed();
            if (!frameId.isEmpty())
            {
                frames.insert(frameId);
            }
            if (!sourceId.isEmpty())
            {
                sourceIds.insert(sourceId);
            }

            const QString astrometricManifest = query.value(2).toString().trimmed();
            const QString spccManifest = query.value(3).toString().trimmed();
            if (!astrometricManifest.isEmpty())
            {
                astrometricManifests.insert(astrometricManifest);
            }
            if (!spccManifest.isEmpty())
            {
                spccManifests.insert(spccManifest);
            }

            if (!query.value(4).isNull())
            {
                const double separation = query.value(4).toDouble();
                if (std::isfinite(separation))
                {
                    summary.matchSeparationsArcsec.append(separation);
                }
            }
            if (!query.value(5).isNull())
            {
                const double residual = query.value(5).toDouble();
                if (std::isfinite(residual))
                {
                    summary.residualsPx.append(residual);
                }
            }

            const QString status = query.value(6).toString().trimmed();
            if (status == QStringLiteral("satisfied"))
            {
                summary.satisfiedCount++;
            }
            else if (status == QStringLiteral("missing_source"))
            {
                summary.missingSourceCount++;
            }
            else if (status == QStringLiteral("missing_chunk_or_cache"))
            {
                summary.missingChunkOrCacheCount++;
            }
            else if (status == QStringLiteral("manifest_mismatch"))
            {
                summary.manifestMismatchCount++;
            }
            else if (status == QStringLiteral("stale_match"))
            {
                summary.staleMatchCount++;
            }
            else if (status == QStringLiteral("rejected_quality_fields"))
            {
                summary.rejectedQualityFieldsCount++;
            }
            else
            {
                summary.untestedCount++;
            }
        }

        summary.frameCount = frames.size();
        summary.sourceCount = sourceIds.size();
        QStringList sortedAstrometricManifests = astrometricManifests.values();
        QStringList sortedSpccManifests = spccManifests.values();
        std::sort(sortedAstrometricManifests.begin(), sortedAstrometricManifests.end());
        std::sort(sortedSpccManifests.begin(), sortedSpccManifests.end());
        summary.astrometricManifestId =
            sortedAstrometricManifests.isEmpty() ? QString() : sortedAstrometricManifests.join(QStringLiteral(", "));
        summary.spccManifestId =
            sortedSpccManifests.isEmpty() ? QString() : sortedSpccManifests.join(QStringLiteral(", "));

        if (summary.referenceCount > 0)
        {
            summaries.append(std::move(summary));
        }
    }

    return summaries;
}

namespace
{
/// The metrics database of one run, opened on the first question and closed with the object.
class SqliteReportMetrics final : public IReportMetrics
{
public:
    SqliteReportMetrics(QString path, WorkflowExecutionCounterAccumulator* counters)
        : m_path(std::move(path)), m_counters(counters)
    {
    }

    ~SqliteReportMetrics() override
    {
        if (m_connectionName.isEmpty())
        {
            return;
        }
        {
            QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
            if (db.isOpen())
            {
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }

    SqliteReportMetrics(const SqliteReportMetrics&) = delete;
    SqliteReportMetrics& operator=(const SqliteReportMetrics&) = delete;

    [[nodiscard]] bool available() override
    {
        return open();
    }

    [[nodiscard]] std::optional<int> settingsVersion(const QString& groupKey) override
    {
        if (!open())
        {
            return std::nullopt;
        }
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        return queryRegistrationSettingsVersion(db, groupKey, m_counters);
    }

    [[nodiscard]] QHash<QString, RegistrationMetricRow> metricsByFrame(const QString& groupKey,
                                                                       const int settingsVersion) override
    {
        if (!open())
        {
            return {};
        }
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        return loadRegistrationMetricsByFrame(db, groupKey, settingsVersion, m_counters);
    }

    [[nodiscard]] QHash<QString, FrameNoiseStatistics> noiseByFrame(const QString& groupKey,
                                                                    const int settingsVersion) override
    {
        if (!open())
        {
            return {};
        }
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        return loadRegistrationNoiseByFrame(db, groupKey, settingsVersion, m_counters);
    }

    [[nodiscard]] QList<CometReportRun> cometRuns(const WorkflowManifest& manifest) override
    {
        return open() ? loadCometReportRuns(m_path, manifest, m_counters) : QList<CometReportRun>{};
    }

    [[nodiscard]] QList<SpccHandoffGroupSummary> catalogHandoff(const WorkflowManifest& manifest) override
    {
        if (!open())
        {
            return {};
        }
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        return loadSpccHandoffSummaries(db, manifest, m_counters);
    }

private:
    /// Opens the database once; a file that is not there or cannot be opened is asked no further.
    [[nodiscard]] bool open()
    {
        if (m_tried)
        {
            return m_open;
        }
        m_tried = true;
        if (m_path.isEmpty() || !QFileInfo::exists(m_path))
        {
            return false;
        }
        m_connectionName =
            QStringLiteral("WorkflowReportModel_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        db.setDatabaseName(m_path);
        m_open = db.open();
        return m_open;
    }

    QString m_path;
    WorkflowExecutionCounterAccumulator* m_counters = nullptr;
    QString m_connectionName;
    bool m_tried = false;
    bool m_open = false;
};

class SessionReportHeaders final : public IReportHeaders
{
public:
    explicit SessionReportHeaders(const an::consumer::EngineSession* session) : m_session(session) {}

    [[nodiscard]] std::optional<ReportHeader> probe(const QString& path) override
    {
        return probeReportHeader(m_session, path);
    }

private:
    const an::consumer::EngineSession* m_session = nullptr;
};

class SessionReportImages final : public IReportImages
{
public:
    explicit SessionReportImages(const an::consumer::EngineSession* session) : m_session(session) {}

    [[nodiscard]] std::optional<ReportImageStatistics> measure(const QString& path, const std::uint32_t width) override
    {
        return analyzeRawImage(m_session, path, width);
    }

private:
    const an::consumer::EngineSession* m_session = nullptr;
};

class HostReportFiles final : public IReportFiles
{
public:
    [[nodiscard]] bool exists(const QString& path) override
    {
        const QFileInfo info(path);
        return info.exists() && info.isFile();
    }

    [[nodiscard]] std::optional<qint64> size(const QString& path) override
    {
        const QFileInfo info(path);
        return info.exists() && info.isFile() ? std::optional<qint64>(info.size()) : std::nullopt;
    }

    [[nodiscard]] std::optional<QDateTime> lastModified(const QString& path) override
    {
        const QFileInfo info(path);
        return info.exists() && info.lastModified().isValid() ? std::optional<QDateTime>(info.lastModified())
                                                              : std::nullopt;
    }

    [[nodiscard]] std::optional<QString> readText(const QString& path) override
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            return std::nullopt;
        }
        return QString::fromUtf8(file.readAll());
    }
};

class HostReportHost final : public IReportHost
{
public:
    [[nodiscard]] QDateTime now() override
    {
        return QDateTime::currentDateTime();
    }

    [[nodiscard]] QString timeZoneId() override
    {
        return QString::fromUtf8(QTimeZone::systemTimeZoneId());
    }

    [[nodiscard]] QString platform() override
    {
        return QStringLiteral("%1 (%2)").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
    }
};
} // namespace

struct ProductionReportSources::Impl
{
    Impl(const an::consumer::EngineSession* sessionIn, QString metricsDatabasePath,
         WorkflowExecutionCounterAccumulator* counters)
        : session(sessionIn), metrics(std::move(metricsDatabasePath), counters), headers(sessionIn), images(sessionIn)
    {
    }

    const an::consumer::EngineSession* session = nullptr;
    SqliteReportMetrics metrics;
    SessionReportHeaders headers;
    SessionReportImages images;
    HostReportFiles files;
    HostReportHost host;
};

ProductionReportSources::ProductionReportSources(const an::consumer::EngineSession* session,
                                                 QString metricsDatabasePath,
                                                 WorkflowExecutionCounterAccumulator* counters)
    : m_impl(std::make_unique<Impl>(session, std::move(metricsDatabasePath), counters))
{
}

ProductionReportSources::~ProductionReportSources() = default;

ReportSources ProductionReportSources::sources() const
{
    return ReportSources{.metrics = &m_impl->metrics,
                         .headers = &m_impl->headers,
                         .images = &m_impl->images,
                         .files = &m_impl->files,
                         .host = &m_impl->host,
                         .session = m_impl->session};
}
} // namespace ImageIntegration::Reporting
