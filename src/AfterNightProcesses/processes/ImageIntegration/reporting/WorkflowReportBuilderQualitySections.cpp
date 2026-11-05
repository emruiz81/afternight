// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowReportBuilderHelpers.h"

#include "WorkflowReportBuilder.h"
#include "WorkflowReportSectionBuilders.h"

#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/ImageIntegration/core/NormalizationUtils.h"
#include "processes/ImageIntegration/core/PhotometricWeightingUiUtils.h"
#include "processes/ImageIntegration/core/NormalizationOptionParsing.h"
#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/core/RejectionMapUtils.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/model/FilterStatistics.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"
#include "processes/core/RegistrationMetricsStateLoader.h"

#include <QCoreApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTextStream>
#include <QTimeZone>
#include <QSysInfo>
#include <QUuid>

#include <cmath>
#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <utility>

/**
 * @file WorkflowReportBuilderQualitySections.cpp
 * @brief Frame-quality, target, delta, diagnostics and run-summary sections of the workflow report.
 *
 * Split out of WorkflowReportBuilder.cpp to keep that unit readable.
 */

namespace ImageIntegration::Reporting
{

void WorkflowReportBuilder::appendFrameQualityWeighting(QString& md) const
{
    appendSectionAnchor(md, QStringLiteral("frame-quality-weighting"));
    md += QStringLiteral("## \U0001F4C9 Frame Quality & Weighting\n");
    md += QStringLiteral("_Interpretation: uses Registration metrics (stars/FWHM/SNR/eccentricity) to summarize frame "
                         "quality scoring and ");
    md += QStringLiteral("the effective weighting profile applied during stacking when weighting is enabled._\n\n");

    if (m_manifest.integrationGroups.empty())
    {
        md += QStringLiteral("_No integration groups were available for frame quality and weighting analysis._\n\n");
        return;
    }

    // What each group's stack recorded of its own weights, which the section shows where the run left a record.
    QHash<QString, const IntegrationGroupResult*> resultByGroupKey;
    for (const auto& groupResult : m_result.integrationGroupResults)
    {
        resultByGroupKey.insert(groupResult.groupKey, &groupResult);
    }

    const QString outputDir = m_manifest.outputDirectory.trimmed();
    const QString metricsDbPath =
        outputDir.isEmpty() ? QString() : QDir(outputDir).filePath(QStringLiteral("registration_metrics.db"));
    if (metricsDbPath.isEmpty() || !QFileInfo::exists(metricsDbPath))
    {
        md += QStringLiteral("_Registration metrics database (`registration_metrics.db`) was not found, so "
                             "quality/weight histograms are unavailable._\n\n");
        return;
    }

    const QString connectionName =
        QStringLiteral("WorkflowReportMetrics_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(metricsDbPath);
    if (!db.open())
    {
        md += QStringLiteral(
            "_Registration metrics database could not be opened, so quality/weight histograms are unavailable._\n\n");
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        return;
    }

    QList<GroupQualityWeightSummary> summaries;
    summaries.reserve(static_cast<int>(m_manifest.integrationGroups.size()));

    for (const auto& group : m_manifest.integrationGroups)
    {
        GroupQualityWeightSummary summary;
        summary.groupName = friendlyGroupKey(group.groupKey.toString());
        // The weighting the group's stack applied: none beside a combination that takes no weights or a merge.
        const StackingSettings applied = ImageIntegration::sanitizeStackingSettings(group.stacking);
        summary.weightingMode =
            applied.weighting.trimmed().isEmpty() ? QStringLiteral("None") : applied.weighting.trimmed();
        summary.minWeightFloor = std::clamp(group.stacking.minWeightFloor, 0.0, 1.0);
        summary.totalFrames = static_cast<int>(group.lightFiles.size());

        const QString registrationKey = group.registrationGroupKey.toString().trimmed();
        if (registrationKey.isEmpty())
        {
            summaries.append(std::move(summary));
            continue;
        }

        const auto settingsVersionOpt = queryRegistrationSettingsVersion(db, registrationKey, m_counters);
        if (!settingsVersionOpt.has_value())
        {
            summaries.append(std::move(summary));
            continue;
        }

        const QHash<QString, RegistrationMetricRow> metricsByFrame =
            loadRegistrationMetricsByFrame(db, registrationKey, *settingsVersionOpt, m_counters);
        if (metricsByFrame.isEmpty())
        {
            summaries.append(std::move(summary));
            continue;
        }

        const an::Result<ImageIntegration::IntegrationStackChoice> choice =
            ImageIntegration::integrationStackChoice(applied);
        const QHash<QString, FrameNoiseStatistics> noiseByFrame =
            choice.hasValue() && choice->weighting == an::StackWeighting::Noise
                ? loadRegistrationNoiseByFrame(db, registrationKey, *settingsVersionOpt, m_counters)
                : QHash<QString, FrameNoiseStatistics>{};

        QList<QualityWeightSample> samples;
        samples.reserve(summary.totalFrames);
        for (int i = 0; i < summary.totalFrames; ++i)
        {
            const QString frameId = group.lightFiles[static_cast<size_t>(i)].canonicalPath();
            const auto metricIt = metricsByFrame.constFind(frameId);
            if (metricIt == metricsByFrame.cend())
            {
                continue;
            }

            summary.framesWithMetrics++;
            if (!metricIt->succeeded)
            {
                continue;
            }

            summary.framesSucceeded++;
            QualityWeightSample sample;
            sample.frameId = frameId;
            sample.exposureSeconds = resolveLightExposureSeconds(group, i);
            sample.metrics.stars = metricIt->stars;
            sample.metrics.fwhm = metricIt->fwhm;
            sample.metrics.snr = metricIt->snr;
            sample.metrics.eccentricity = metricIt->eccentricity;
            sample.metrics.photometric_zero_point = metricIt->photometricZeroPoint;
            sample.metrics.zero_point_scatter = metricIt->zeroPointScatter;
            sample.metrics.astrometric_rms_pixels = metricIt->astrometricRmsPixels;
            sample.metrics.astrometric_rms_arcsec = metricIt->astrometricRmsArcsec;
            sample.metrics.limiting_magnitude = metricIt->limitingMagnitude;
            sample.metrics.catalog_matched_stars = metricIt->catalogMatchedStars;
            sample.metrics.succeeded = true;
            sample.noise = noiseByFrame.value(frameId);
            samples.append(std::move(sample));
        }

        if (!samples.isEmpty())
        {
            const IntegrationGroupResult* groupResult = resultByGroupKey.value(group.groupKey.toString(), nullptr);
            summary.weights =
                groupResult != nullptr && groupResult->success && !groupResult->qualityReport.lightWeights.empty()
                    ? recordedWeightsForSamples(samples, groupResult->qualityReport, group)
                    : computeWeightsForSamples(m_session, samples, group);
            summary.medianWeight = medianOf(summary.weights);

            std::vector<FrameQualityMetrics> metricsForStats;
            metricsForStats.reserve(static_cast<size_t>(samples.size()));
            for (const auto& sample : samples)
            {
                metricsForStats.push_back(sample.metrics);
            }

            const QString filterName =
                group.filter.trimmed().isEmpty() ? QStringLiteral("Unfiltered") : group.filter.trimmed();
            const FilterStatistics filterStats = FilterStatisticsEngine::computeStatistics(filterName, metricsForStats);
            const std::optional<FilterStatistics> filterStatsOpt =
                metricsForStats.empty() ? std::nullopt : std::optional<FilterStatistics>(filterStats);

            // Only the star-count and width weights are raised to the floor on the scale the report shows.
            const bool floorShown = showsWeightFloor(summary.weightingMode);
            summary.contributions.reserve(samples.size());
            for (int i = 0; i < samples.size() && i < summary.weights.size(); ++i)
            {
                const auto& sample = samples[i];
                const double weight = summary.weights[i];
                const double weightedSeconds = (sample.exposureSeconds > 0.0 && std::isfinite(sample.exposureSeconds))
                                                   ? sample.exposureSeconds * weight
                                                   : 0.0;
                summary.weightedIntegrationMs += static_cast<int>(std::llround(weightedSeconds * 1000.0));
                if (floorShown && weight <= summary.minWeightFloor + 1e-6)
                {
                    summary.framesAtMinFloor++;
                }

                const double score = FilterStatisticsEngine::computeQualityScore(sample.metrics, filterStatsOpt);
                std::optional<double> qualityScore;
                if (std::isfinite(score))
                {
                    summary.qualityScores.append(score);
                    qualityScore = score;
                }
                if (std::isfinite(sample.metrics.photometric_zero_point) &&
                    std::abs(sample.metrics.photometric_zero_point) > 1.0e-12)
                {
                    summary.zeroPoints.append(sample.metrics.photometric_zero_point);
                }
                if (std::isfinite(sample.metrics.astrometric_rms_arcsec) && sample.metrics.astrometric_rms_arcsec > 0.0)
                {
                    summary.astrometricRmsArcsec.append(sample.metrics.astrometric_rms_arcsec);
                }
                if (sample.metrics.catalog_matched_stars > 0)
                {
                    summary.catalogMatchedStars.append(static_cast<double>(sample.metrics.catalog_matched_stars));
                }

                WeightedFrameContribution contribution;
                contribution.frameId = sample.frameId;
                contribution.frameLabel = QFileInfo(sample.frameId).fileName();
                if (contribution.frameLabel.isEmpty())
                {
                    contribution.frameLabel = sample.frameId;
                }
                contribution.exposureSeconds = sample.exposureSeconds;
                contribution.weight = weight;
                contribution.qualityScore = qualityScore;
                contribution.weightedSeconds = weightedSeconds;
                summary.contributions.append(std::move(contribution));
            }

            std::sort(summary.contributions.begin(), summary.contributions.end(),
                      [](const WeightedFrameContribution& lhs, const WeightedFrameContribution& rhs) {
                          return lhs.weightedSeconds > rhs.weightedSeconds;
                      });
        }

        summaries.append(std::move(summary));
    }

    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);

    if (summaries.isEmpty())
    {
        md += QStringLiteral("_No registration-derived frame quality metrics were available for this run._\n\n");
        return;
    }

    int totalFrames = 0;
    int totalFramesSucceeded = 0;
    int totalFramesAtFloor = 0;
    int totalWeightedMs = 0;
    QList<double> allWeights;
    QList<double> allQualityScores;
    QList<double> allZeroPoints;
    QList<double> allAstrometricRms;
    QList<double> allCatalogMatched;
    QSet<QString> weightingModes;
    bool anyFloorShown = false;

    for (const auto& summary : summaries)
    {
        anyFloorShown = anyFloorShown || showsWeightFloor(summary.weightingMode);
        totalFrames += summary.totalFrames;
        totalFramesSucceeded += summary.framesSucceeded;
        totalFramesAtFloor += summary.framesAtMinFloor;
        totalWeightedMs += summary.weightedIntegrationMs;
        allWeights.append(summary.weights);
        allQualityScores.append(summary.qualityScores);
        allZeroPoints.append(summary.zeroPoints);
        allAstrometricRms.append(summary.astrometricRmsArcsec);
        allCatalogMatched.append(summary.catalogMatchedStars);
        weightingModes.insert(summary.weightingMode);
    }

    const double usedPct =
        totalFrames > 0 ? static_cast<double>(totalFramesSucceeded) * 100.0 / static_cast<double>(totalFrames) : 0.0;
    const std::optional<double> medianWeightOverall = medianOf(allWeights);
    const std::optional<double> medianQualityOverall = medianOf(allQualityScores);
    const std::optional<double> medianZeroPointOverall = medianOf(allZeroPoints);
    const std::optional<double> medianAstrometricRmsOverall = medianOf(allAstrometricRms);
    const std::optional<double> medianCatalogMatchedOverall = medianOf(allCatalogMatched);

    QString weightingModeSummary;
    if (weightingModes.isEmpty())
    {
        weightingModeSummary = QStringLiteral("None");
    }
    else if (weightingModes.size() == 1)
    {
        weightingModeSummary = *weightingModes.constBegin();
    }
    else
    {
        QStringList modes = weightingModes.values();
        std::sort(modes.begin(), modes.end(),
                  [](const QString& lhs, const QString& rhs) { return lhs.compare(rhs, Qt::CaseInsensitive) < 0; });
        weightingModeSummary = QStringLiteral("Varies (%1)").arg(modes.join(QStringLiteral(", ")));
    }

    md += QStringLiteral("<div class=\"an-weighting-kpi-grid\">");
    md +=
        QStringLiteral("<div class=\"an-weighting-kpi-card\"><div class=\"an-weighting-kpi-label\">Frames Used</div>");
    md += QStringLiteral("<div class=\"an-weighting-kpi-value an-num\">%1 / %2</div>")
              .arg(totalFramesSucceeded)
              .arg(totalFrames);
    md += QStringLiteral("<div class=\"an-weighting-kpi-hint\">%1 with succeeded registration metrics</div></div>")
              .arg(formatPercent(usedPct).toHtmlEscaped());

    md += QStringLiteral(
        "<div class=\"an-weighting-kpi-card\"><div class=\"an-weighting-kpi-label\">Weighted Integration Time</div>");
    md += QStringLiteral("<div class=\"an-weighting-kpi-value an-num\">%1</div>")
              .arg(totalWeightedMs > 0 ? formatDuration(totalWeightedMs).toHtmlEscaped() : QStringLiteral("n/a"));
    md += QStringLiteral(
        "<div class=\"an-weighting-kpi-hint\">sum(exposure \u00D7 weight) across used frames</div></div>");

    md += QStringLiteral(
        "<div class=\"an-weighting-kpi-card\"><div class=\"an-weighting-kpi-label\">Median Weight</div>");
    md += QStringLiteral("<div class=\"an-weighting-kpi-value an-num\">%1</div>")
              .arg(medianWeightOverall.has_value() ? QString::number(*medianWeightOverall, 'f', 3).toHtmlEscaped()
                                                   : QStringLiteral("n/a"));
    md += QStringLiteral("<div class=\"an-weighting-kpi-hint\">higher means stronger frame contribution</div></div>");

    md += QStringLiteral(
        "<div class=\"an-weighting-kpi-card\"><div class=\"an-weighting-kpi-label\">Median Quality Score</div>");
    md += QStringLiteral("<div class=\"an-weighting-kpi-value an-num\">%1</div>")
              .arg(medianQualityOverall.has_value() ? QString::number(*medianQualityOverall, 'f', 1).toHtmlEscaped()
                                                    : QStringLiteral("n/a"));
    md += QStringLiteral(
        "<div class=\"an-weighting-kpi-hint\">registration-tab compatible quality score (0-100)</div></div>");

    if (medianZeroPointOverall.has_value())
    {
        md += QStringLiteral(
            "<div class=\"an-weighting-kpi-card\"><div class=\"an-weighting-kpi-label\">Median Zero Point</div>");
        md += QStringLiteral("<div class=\"an-weighting-kpi-value an-num\">%1</div>")
                  .arg(QString::number(*medianZeroPointOverall, 'f', 2).toHtmlEscaped());
        md += QStringLiteral("<div class=\"an-weighting-kpi-hint\">higher suggests better transparency</div></div>");
    }

    if (medianAstrometricRmsOverall.has_value())
    {
        md += QStringLiteral(
            "<div class=\"an-weighting-kpi-card\"><div class=\"an-weighting-kpi-label\">Median Astrometric RMS</div>");
        md += QStringLiteral("<div class=\"an-weighting-kpi-value an-num\">%1 arcsec</div>")
                  .arg(QString::number(*medianAstrometricRmsOverall, 'f', 2).toHtmlEscaped());
        md += QStringLiteral(
            "<div class=\"an-weighting-kpi-hint\">lower indicates tighter astrometric residuals</div></div>");
    }

    if (medianCatalogMatchedOverall.has_value())
    {
        md += QStringLiteral(
            "<div class=\"an-weighting-kpi-card\"><div class=\"an-weighting-kpi-label\">Median Catalog Matches</div>");
        md += QStringLiteral("<div class=\"an-weighting-kpi-value an-num\">%1</div>")
                  .arg(QString::number(*medianCatalogMatchedOverall, 'f', 0).toHtmlEscaped());
        md += QStringLiteral(
            "<div class=\"an-weighting-kpi-hint\">matched catalog stars per successful frame</div></div>");
    }

    md += QStringLiteral(
        "<div class=\"an-weighting-kpi-card\"><div class=\"an-weighting-kpi-label\">Weighting Mode</div>");
    md += QStringLiteral("<div class=\"an-weighting-kpi-value\">%1</div>").arg(weightingModeSummary.toHtmlEscaped());
    if (weightingModes.size() == 1 && ImageIntegration::isPhotometricWeightingMode(weightingModeSummary))
    {
        md += QStringLiteral("<div class=\"an-weighting-kpi-hint\">1.0 = average frame contribution after "
                             "mean-normalization</div></div>");
    }
    else if (anyFloorShown)
    {
        md += QStringLiteral("<div class=\"an-weighting-kpi-hint\">%1 frame(s) at minimum-weight floor</div></div>")
                  .arg(totalFramesAtFloor);
    }
    else
    {
        md += QStringLiteral("<div class=\"an-weighting-kpi-hint\">1.0 = the heaviest frame's weight</div></div>");
    }
    md += QStringLiteral("</div>\n\n");

    if (!allWeights.isEmpty() || !allQualityScores.isEmpty())
    {
        md += QStringLiteral("<div class=\"an-quality-hist-grid\">");
        if (!allWeights.isEmpty())
        {
            const bool aggregatePhotometric =
                weightingModes.size() == 1 && ImageIntegration::isPhotometricWeightingMode(weightingModeSummary);
            const auto [weightMin, weightMax] =
                aggregatePhotometric ? observedRange(allWeights) : std::pair<double, double>{0.0, 1.0};
            const std::vector<int> weightBins = buildHistogram(allWeights, weightMin, weightMax, 12);
            std::optional<int> markerBin;
            if (!aggregatePhotometric && weightingModes.size() == 1 && showsWeightFloor(weightingModeSummary))
            {
                double floor = 0.0;
                for (const auto& summary : summaries)
                {
                    floor = (std::max)(floor, summary.minWeightFloor);
                }
                int index = static_cast<int>(floor * 12.0);
                if (index >= 12)
                    index = 11;
                if (index < 0)
                    index = 0;
                markerBin = index;
            }
            md += buildHistogramCardHtml(
                QStringLiteral("Weight Distribution"),
                aggregatePhotometric
                    ? QStringLiteral("Per-frame integration weights (1.0 = average frame contribution)")
                    : QStringLiteral("Per-frame integration weights (0.0 to 1.0)"),
                weightBins, QString::number(weightMin, 'f', aggregatePhotometric ? 2 : 1),
                QString::number(weightMax, 'f', aggregatePhotometric ? 2 : 1), markerBin);
        }
        if (!allQualityScores.isEmpty())
        {
            const std::vector<int> qualityBins = buildHistogram(allQualityScores, 0.0, 100.0, 10);
            md += buildHistogramCardHtml(QStringLiteral("Quality Score Distribution"),
                                         QStringLiteral("Registration quality score histogram (higher is better)"),
                                         qualityBins, QStringLiteral("0"), QStringLiteral("100"), std::nullopt);
        }
        md += QStringLiteral("</div>\n\n");
    }

    // Check for per-pixel (local) weighting across groups.
    {
        QStringList localWeightingGroups;
        for (const auto& ig : m_manifest.integrationGroups)
        {
            if (ig.stacking.localWeighting.enabled)
            {
                localWeightingGroups.append(friendlyGroupKey(ig.groupKey.toString()));
            }
        }
        if (!localWeightingGroups.isEmpty())
        {
            md += QStringLiteral("<div class=\"an-info-banner\">");
            md += QStringLiteral("<strong>Per-Pixel (Spatially Varying) Weighting</strong> is active for: ");
            md += localWeightingGroups.join(QStringLiteral(", ")).toHtmlEscaped();
            md += QStringLiteral(". The histogram above shows per-frame <em>global</em> weights. ");
            md += QStringLiteral("Spatial weight variation is reported per-group in the <a "
                                 "href=\"#stacked-masters\">Stacked Masters</a> section.");
            md += QStringLiteral("</div>\n\n");
        }
    }

    md += QStringLiteral("| Integration Group | Weighting Mode | Frames (used/total) | Median Weight | Weighted Time | "
                         "Median Quality | Median Zero Point | Median Astrometric RMS | Median Catalog Matches |\n");
    md += QStringLiteral("|---|---|---|---|---|---|---|---|---|\n");
    for (const auto& summary : summaries)
    {
        const std::optional<double> qualityMedian = medianOf(summary.qualityScores);
        const std::optional<double> zeroPointMedian = medianOf(summary.zeroPoints);
        const std::optional<double> astrometricMedian = medianOf(summary.astrometricRmsArcsec);
        const std::optional<double> catalogMatchedMedian = medianOf(summary.catalogMatchedStars);
        md +=
            QStringLiteral(
                "| %1 | %2 | <span class=\"an-num\">%3/%4</span> | <span class=\"an-num\">%5</span> | <span "
                "class=\"an-num\">%6</span> | <span class=\"an-num\">%7</span> | <span class=\"an-num\">%8</span> | "
                "<span class=\"an-num\">%9</span> | <span class=\"an-num\">%10</span> |\n")
                .arg(summary.groupName.toHtmlEscaped())
                .arg(summary.weightingMode.toHtmlEscaped())
                .arg(summary.framesSucceeded)
                .arg(summary.totalFrames)
                .arg(summary.medianWeight.has_value() ? QString::number(*summary.medianWeight, 'f', 3).toHtmlEscaped()
                                                      : QStringLiteral("n/a"))
                .arg(summary.weightedIntegrationMs > 0 ? formatDuration(summary.weightedIntegrationMs).toHtmlEscaped()
                                                       : QStringLiteral("n/a"))
                .arg(qualityMedian.has_value() ? QString::number(*qualityMedian, 'f', 1).toHtmlEscaped()
                                               : QStringLiteral("n/a"))
                .arg(zeroPointMedian.has_value() ? QString::number(*zeroPointMedian, 'f', 2).toHtmlEscaped()
                                                 : QStringLiteral("n/a"))
                .arg(astrometricMedian.has_value()
                         ? QStringLiteral("%1 arcsec").arg(QString::number(*astrometricMedian, 'f', 2)).toHtmlEscaped()
                         : QStringLiteral("n/a"))
                .arg(catalogMatchedMedian.has_value() ? QString::number(*catalogMatchedMedian, 'f', 0).toHtmlEscaped()
                                                      : QStringLiteral("n/a"));
    }
    md += QStringLiteral("\n");

    md += QStringLiteral("### Per-Group Histograms & Top Contributors\n\n");
    md += QStringLiteral(
        "_Each card uses the same metrics as the aggregate section, but scoped to one integration group._\n\n");

    for (const auto& summary : summaries)
    {
        const bool hasWeightHist = !summary.weights.isEmpty();
        const bool hasQualityHist = !summary.qualityScores.isEmpty();
        const bool hasContributors = !summary.contributions.isEmpty();

        if (!hasWeightHist && !hasQualityHist && !hasContributors)
        {
            continue;
        }

        md += QStringLiteral("<details>\n");
        md += QStringLiteral("<summary>%1 \u2022 %2/%3 frame(s) with metrics</summary>\n\n")
                  .arg(summary.groupName.toHtmlEscaped())
                  .arg(summary.framesSucceeded)
                  .arg(summary.totalFrames);

        if (hasWeightHist || hasQualityHist)
        {
            md += QStringLiteral("<div class=\"an-quality-hist-grid\">");
            if (hasWeightHist)
            {
                std::optional<int> markerBin;
                const bool photometricWeighting = ImageIntegration::isPhotometricWeightingMode(summary.weightingMode);
                if (showsWeightFloor(summary.weightingMode))
                {
                    int index = static_cast<int>(summary.minWeightFloor * 12.0);
                    if (index >= 12)
                        index = 11;
                    if (index < 0)
                        index = 0;
                    markerBin = index;
                }
                const auto [weightMin, weightMax] =
                    photometricWeighting ? observedRange(summary.weights) : std::pair<double, double>{0.0, 1.0};
                const std::vector<int> bins = buildHistogram(summary.weights, weightMin, weightMax, 12);
                md += buildHistogramCardHtml(
                    QStringLiteral("Weight Distribution"),
                    photometricWeighting
                        ? QStringLiteral("%1 \u2022 mode: %2 \u2022 1.0 = average frame contribution")
                              .arg(summary.groupName, summary.weightingMode)
                        : QStringLiteral("%1 \u2022 mode: %2").arg(summary.groupName, summary.weightingMode),
                    bins, QString::number(weightMin, 'f', photometricWeighting ? 2 : 1),
                    QString::number(weightMax, 'f', photometricWeighting ? 2 : 1), markerBin);
            }
            if (hasQualityHist)
            {
                const std::vector<int> bins = buildHistogram(summary.qualityScores, 0.0, 100.0, 10);
                md += buildHistogramCardHtml(
                    QStringLiteral("Quality Score Distribution"),
                    QStringLiteral("%1 \u2022 registration-derived quality score").arg(summary.groupName), bins,
                    QStringLiteral("0"), QStringLiteral("100"), std::nullopt);
            }
            md += QStringLiteral("</div>\n\n");
        }

        if (hasContributors)
        {
            const double totalWeightedSeconds =
                (std::max)(0.0, static_cast<double>(summary.weightedIntegrationMs) / 1000.0);
            md += QStringLiteral("**Top contributing frames (Pareto by weighted exposure)**\n\n");
            md += QStringLiteral("| Frame | Weight | Exposure | Weighted Contribution | Contribution % | Quality |\n");
            md += QStringLiteral("|---|---|---|---|---|---|\n");

            const int maxRows = (std::min)(5, static_cast<int>(summary.contributions.size()));
            for (int i = 0; i < maxRows; ++i)
            {
                const auto& c = summary.contributions[i];
                const QString exposureText = (c.exposureSeconds > 0.0 && std::isfinite(c.exposureSeconds))
                                                 ? QStringLiteral("%1s").arg(QString::number(
                                                       c.exposureSeconds, 'f', (c.exposureSeconds < 10.0 ? 1 : 0)))
                                                 : QStringLiteral("n/a");
                const QString weightedText = (c.weightedSeconds > 0.0 && std::isfinite(c.weightedSeconds))
                                                 ? QStringLiteral("%1s").arg(QString::number(c.weightedSeconds, 'f', 1))
                                                 : QStringLiteral("n/a");
                const QString pctText = (totalWeightedSeconds > 0.0 && c.weightedSeconds > 0.0)
                                            ? formatPercent((c.weightedSeconds * 100.0) / totalWeightedSeconds)
                                            : QStringLiteral("n/a");
                const QString qualityText =
                    c.qualityScore.has_value() ? QString::number(*c.qualityScore, 'f', 1) : QStringLiteral("n/a");

                md += QStringLiteral("| `%1` | <span class=\"an-num\">%2</span> | <span class=\"an-num\">%3</span> | "
                                     "<span class=\"an-num\">%4</span> | <span class=\"an-num\">%5</span> | <span "
                                     "class=\"an-num\">%6</span> |\n")
                          .arg(c.frameLabel.toHtmlEscaped())
                          .arg(QString::number(c.weight, 'f', 3).toHtmlEscaped())
                          .arg(exposureText.toHtmlEscaped())
                          .arg(weightedText.toHtmlEscaped())
                          .arg(pctText.toHtmlEscaped())
                          .arg(qualityText.toHtmlEscaped());
            }
            md += QStringLiteral("\n");
        }

        md += QStringLiteral("</details>\n\n");
    }
}

void WorkflowReportBuilder::appendTargetSummaries(QString& md) const
{
    appendSectionAnchor(md, QStringLiteral("target-summaries"));
    md += QStringLiteral("## \U0001F3AF Target Summaries\n");
    md += QStringLiteral("_Per-target rollups: SNR proxy is mean/background-noise from unstretched masters; ");
    md += QStringLiteral("overlap retention is stacked area relative to source capture area._\n\n");

    if (m_manifest.integrationGroups.empty())
    {
        md += QStringLiteral("_No integration groups were available to summarize by target._\n\n");
        return;
    }

    struct TargetAggregate
    {
        int totalFrames = 0;
        int totalIntegrationMs = 0;
        QSet<QString> filters;
        QSet<QString> masterReferences;
        QList<double> snrProxies;
        QList<double> overlapRetentionPcts;
    };

    QHash<QString, const IntegrationGroupResult*> resultByGroupKey;
    resultByGroupKey.reserve(static_cast<int>(m_result.integrationGroupResults.size()));
    for (const auto& groupResult : m_result.integrationGroupResults)
    {
        resultByGroupKey.insert(groupResult.groupKey, &groupResult);
    }

    QMap<QString, TargetAggregate> aggregatesByTarget;
    for (const auto& group : m_manifest.integrationGroups)
    {
        QString target = resolveTargetName(group);
        if (target.isEmpty())
        {
            target = QStringLiteral("Unspecified Target");
        }

        TargetAggregate& aggregate = aggregatesByTarget[target];
        aggregate.totalFrames += static_cast<int>(group.lightFiles.size());
        aggregate.totalIntegrationMs += buildExposureBreakdown(group).totalMs;

        const QString filter = group.filter.trimmed();
        if (!filter.isEmpty())
        {
            aggregate.filters.insert(filter);
        }

        if (!group.outputFilename.trimmed().isEmpty())
        {
            aggregate.masterReferences.insert(group.outputFilename.trimmed());
        }

        const IntegrationGroupResult* groupResult = resultByGroupKey.value(group.groupKey.toString(), nullptr);
        if (!groupResult || !groupResult->success)
        {
            continue;
        }

        if (!groupResult->outputFilename.trimmed().isEmpty())
        {
            aggregate.masterReferences.insert(groupResult->outputFilename.trimmed());
        }

        QString rawStatsPath = resolvePathForRead(groupResult->outputPath);
        if (rawStatsPath.isEmpty() && !groupResult->outputFilename.trimmed().isEmpty())
        {
            rawStatsPath = resolvePathForRead(QDir(m_manifest.outputDirectory).filePath(groupResult->outputFilename));
        }
        const auto stats = analyzeRawImageCached(rawStatsPath);
        if (stats.has_value())
        {
            const auto meanOpt =
                averageChannelStat(stats->channelStats, [](const RawChannelStats& c) { return c.mean; });
            const auto bgNoiseOpt =
                averageChannelStat(stats->channelStats, [](const RawChannelStats& c) { return c.backgroundNoise; });
            if (meanOpt.has_value() && bgNoiseOpt.has_value() && *bgNoiseOpt > 0.0)
            {
                aggregate.snrProxies.append(*meanOpt / *bgNoiseOpt);
            }

            const auto sourceDims = parseResolutionDimensions(group.resolution);
            if (sourceDims.has_value())
            {
                const double sourceArea =
                    static_cast<double>(sourceDims->first) * static_cast<double>(sourceDims->second);
                const double outputArea = static_cast<double>(stats->width) * static_cast<double>(stats->height);
                if (sourceArea > 0.0 && outputArea > 0.0)
                {
                    const double retainedPct = std::clamp(outputArea * 100.0 / sourceArea, 0.0, 100.0);
                    aggregate.overlapRetentionPcts.append(retainedPct);
                }
            }
        }
    }

    if (aggregatesByTarget.isEmpty())
    {
        md += QStringLiteral("_No target data was resolved for this report._\n\n");
        return;
    }

    const auto sortedValues = [](const QSet<QString>& values) {
        QStringList list = values.values();
        std::sort(list.begin(), list.end(),
                  [](const QString& lhs, const QString& rhs) { return lhs.compare(rhs, Qt::CaseInsensitive) < 0; });
        return list;
    };
    const auto averageOf = [](const QList<double>& values) -> std::optional<double> {
        if (values.isEmpty())
        {
            return std::nullopt;
        }
        double total = 0.0;
        int count = 0;
        for (const double value : values)
        {
            if (!std::isfinite(value))
            {
                continue;
            }
            total += value;
            ++count;
        }
        if (count <= 0)
        {
            return std::nullopt;
        }
        return total / static_cast<double>(count);
    };

    md += QStringLiteral("<div class=\"an-target-summary-grid\">");
    for (auto it = aggregatesByTarget.cbegin(); it != aggregatesByTarget.cend(); ++it)
    {
        const QString target = it.key();
        const TargetAggregate& aggregate = it.value();

        const QStringList filters = sortedValues(aggregate.filters);
        const QStringList masters = sortedValues(aggregate.masterReferences);

        const std::optional<double> snrAvg = averageOf(aggregate.snrProxies);
        const std::optional<double> overlapAvg = averageOf(aggregate.overlapRetentionPcts);

        QString mastersHtml = QStringLiteral("-");
        if (!masters.isEmpty())
        {
            QStringList encoded;
            encoded.reserve(masters.size());
            for (const auto& master : masters)
            {
                encoded << QStringLiteral("<code>%1</code>").arg(master.toHtmlEscaped());
            }
            mastersHtml = encoded.join(QStringLiteral(", "));
        }

        md += QStringLiteral("<article class=\"an-target-summary-card\">");
        md += QStringLiteral("<h3>%1</h3>").arg(target.toHtmlEscaped());
        md += QStringLiteral("<ul>");
        md += QStringLiteral("<li><strong>Total frames</strong>: %1</li>").arg(aggregate.totalFrames);
        md += QStringLiteral("<li><strong>Total integration time</strong>: %1</li>")
                  .arg(aggregate.totalIntegrationMs > 0 ? formatDuration(aggregate.totalIntegrationMs)
                                                        : QStringLiteral("-"));
        md += QStringLiteral("<li><strong>Filters</strong>: %1</li>")
                  .arg(filters.isEmpty() ? QStringLiteral("-") : filters.join(QStringLiteral(", ")).toHtmlEscaped());
        md += QStringLiteral("<li><strong>Output masters</strong>: %1</li>").arg(mastersHtml);
        md += QStringLiteral("<li><strong>SNR proxy</strong>: %1</li>")
                  .arg(snrAvg.has_value() ? formatSnrProxyValue(*snrAvg).toHtmlEscaped() : QStringLiteral("-"));
        md += QStringLiteral("<li><strong>Overlap retention</strong>: %1</li>")
                  .arg(overlapAvg.has_value() ? formatPercent(*overlapAvg).toHtmlEscaped() : QStringLiteral("-"));
        md += QStringLiteral("</ul>");
        md += QStringLiteral("</article>");
    }
    md += QStringLiteral("</div>\n\n");
}

void WorkflowReportBuilder::appendReproducibilityFingerprint(QString& md) const
{
    appendSectionAnchor(md, QStringLiteral("reproducibility-fingerprint"));
    md += QStringLiteral("## \U0001F9EA Reproducibility Fingerprint\n");
    md += QStringLiteral("_Deterministic fingerprint for this execution context and inputs._\n\n");

    QString manifestSettingsFingerprint;
    QString inputFingerprint;
    const QString compositeFingerprint =
        computeReproducibilityFingerprint(&manifestSettingsFingerprint, &inputFingerprint);

    md += QStringLiteral("| Component | Value |\n");
    md += QStringLiteral("|---|---|\n");
    md += QStringLiteral("| Composite Fingerprint | `%1` |\n").arg(compositeFingerprint);
    md += QStringLiteral("| Manifest/Settings Fingerprint | `%1` |\n").arg(manifestSettingsFingerprint);
    md += QStringLiteral("| Input Fingerprint | `%1` |\n").arg(inputFingerprint);
    md += QStringLiteral("| AfterNight Version | %1 |\n\n").arg(resolveAppVersion(m_reportContext));
}

void WorkflowReportBuilder::appendComparedToPreviousRunDeltas(QString& md) const
{
    appendSectionAnchor(md, QStringLiteral("previous-run-deltas"));
    md += QStringLiteral("## \U0001F4C8 Compared to Previous Successful Run\n");
    md += QStringLiteral("_Deltas are Current minus Previous. Positive values mean increase in this run; ");
    md += QStringLiteral("negative values mean decrease._\n\n");

    if (m_reportBaseDir.trimmed().isEmpty())
    {
        md += QStringLiteral("_Run history path is unavailable, so delta comparison could not be computed._\n\n");
        return;
    }

    const auto previousRun = findPreviousSuccessfulRun(m_reportBaseDir);
    if (!previousRun.has_value())
    {
        md += QStringLiteral(
            "_No previous successful archive was found in this history, so deltas are unavailable._\n\n");
        return;
    }

    const auto analyzer = [this](const QString& path) { return analyzeRawImageCached(path); };
    const RunDeltaMetrics currentMetrics =
        computeRunDeltaMetrics(m_manifest, m_result.integrationGroupResults, m_reportBaseDir, analyzer);
    const RunDeltaMetrics previousMetrics =
        computeRunDeltaMetrics(previousRun->manifest, previousRun->integrationGroupResults,
                               QFileInfo(previousRun->manifestPath).absolutePath(), analyzer);

    const auto formatSignedNumber = [](int delta) {
        return delta >= 0 ? QStringLiteral("+%1").arg(delta) : QString::number(delta);
    };
    const auto formatSignedSize = [&](qint64 deltaBytes) {
        if (deltaBytes == 0)
        {
            return QStringLiteral("0 B");
        }
        const qint64 magnitudeBytes = deltaBytes >= 0 ? deltaBytes : -deltaBytes;
        const QString magnitude = formatFileSize(magnitudeBytes);
        return deltaBytes > 0 ? QStringLiteral("+%1").arg(magnitude) : QStringLiteral("-%1").arg(magnitude);
    };
    const auto formatSignedDuration = [&](int deltaMs) {
        if (deltaMs == 0)
        {
            return QStringLiteral("0s");
        }
        const QString magnitude = formatDuration(std::abs(deltaMs));
        return deltaMs > 0 ? QStringLiteral("+%1").arg(magnitude) : QStringLiteral("-%1").arg(magnitude);
    };
    const auto formatOptional = [](const std::optional<double>& value) {
        return value.has_value() ? formatStatValue(*value) : QStringLiteral("n/a");
    };
    const auto formatOptionalSnr = [](const std::optional<double>& value) {
        return value.has_value() ? formatSnrProxyValue(*value) : QStringLiteral("n/a");
    };
    const auto formatOptionalPercent = [](const std::optional<double>& value) {
        return value.has_value() ? formatPercent(*value) : QStringLiteral("n/a");
    };
    const auto formatOptionalSigned = [](const std::optional<double>& current, const std::optional<double>& previous) {
        if (!current.has_value() || !previous.has_value())
        {
            return QStringLiteral("n/a");
        }
        const double delta = *current - *previous;
        const QString magnitude = formatStatValue(std::abs(delta));
        return delta >= 0.0 ? QStringLiteral("+%1").arg(magnitude) : QStringLiteral("-%1").arg(magnitude);
    };
    const auto formatOptionalSignedSnr = [](const std::optional<double>& current,
                                            const std::optional<double>& previous) {
        if (!current.has_value() || !previous.has_value())
        {
            return QStringLiteral("n/a");
        }
        const double delta = *current - *previous;
        const QString magnitude = formatSnrProxyValue(std::abs(delta));
        return delta >= 0.0 ? QStringLiteral("+%1").arg(magnitude) : QStringLiteral("-%1").arg(magnitude);
    };
    const auto formatOptionalSignedPercent = [](const std::optional<double>& current,
                                                const std::optional<double>& previous) {
        if (!current.has_value() || !previous.has_value())
        {
            return QStringLiteral("n/a");
        }
        const double delta = *current - *previous;
        const QString magnitude = formatPercent(std::abs(delta));
        return delta >= 0.0 ? QStringLiteral("+%1").arg(magnitude) : QStringLiteral("-%1").arg(magnitude);
    };

    const QString previousRunWhen = formatHumanDateTime(previousRun->archivedAtLocal, QDateTime::currentDateTime());
    md += QStringLiteral("_Baseline run: `%1`").arg(previousRun->executionDirName.toHtmlEscaped());
    if (!previousRun->archivedAtLocal.trimmed().isEmpty())
    {
        md += QStringLiteral(" (archived %1)").arg(previousRunWhen.toHtmlEscaped());
    }
    md += QStringLiteral("._\n\n");

    md += QStringLiteral("| Metric | Current | Previous | Delta |\n");
    md += QStringLiteral("|---|---|---|---|\n");
    md += QStringLiteral("| Light Frames | <span class=\"an-num\">%1</span> | <span class=\"an-num\">%2</span> | <span "
                         "class=\"an-num\">%3</span> |\n")
              .arg(currentMetrics.totalLightFrames)
              .arg(previousMetrics.totalLightFrames)
              .arg(formatSignedNumber(currentMetrics.totalLightFrames - previousMetrics.totalLightFrames));
    md += QStringLiteral("| Integration Time | <span class=\"an-num\">%1</span> | <span class=\"an-num\">%2</span> | "
                         "<span class=\"an-num\">%3</span> |\n")
              .arg(formatDuration(currentMetrics.totalIntegrationMs))
              .arg(formatDuration(previousMetrics.totalIntegrationMs))
              .arg(formatSignedDuration(currentMetrics.totalIntegrationMs - previousMetrics.totalIntegrationMs));
    md += QStringLiteral("| Output Size | <span class=\"an-num\">%1</span> | <span class=\"an-num\">%2</span> | <span "
                         "class=\"an-num\">%3</span> |\n")
              .arg(formatFileSize(currentMetrics.totalOutputBytes))
              .arg(formatFileSize(previousMetrics.totalOutputBytes))
              .arg(formatSignedSize(currentMetrics.totalOutputBytes - previousMetrics.totalOutputBytes));
    md += QStringLiteral("| Quality Metrics (SNR Proxy) | <span class=\"an-num\">%1</span> | <span "
                         "class=\"an-num\">%2</span> | <span class=\"an-num\">%3</span> |\n")
              .arg(formatOptionalSnr(currentMetrics.avgSnrProxy))
              .arg(formatOptionalSnr(previousMetrics.avgSnrProxy))
              .arg(formatOptionalSignedSnr(currentMetrics.avgSnrProxy, previousMetrics.avgSnrProxy));
    md += QStringLiteral("| Quality Metrics (Overlap Retention) | <span class=\"an-num\">%1</span> | <span "
                         "class=\"an-num\">%2</span> | <span class=\"an-num\">%3</span> |\n\n")
              .arg(formatOptionalPercent(currentMetrics.avgOverlapRetentionPct))
              .arg(formatOptionalPercent(previousMetrics.avgOverlapRetentionPct))
              .arg(formatOptionalSignedPercent(currentMetrics.avgOverlapRetentionPct,
                                               previousMetrics.avgOverlapRetentionPct));
}

void WorkflowReportBuilder::appendFailureDiagnostics(QString& md) const
{
    appendSectionAnchor(md, QStringLiteral("failure-diagnostics"));
    md += QStringLiteral("## \U0001FA7A Failure Diagnostics\n");

    const bool shouldShowDiagnostics = !m_result.success || m_result.partialSuccess || !m_result.errors.empty();
    if (!shouldShowDiagnostics)
    {
        md += QStringLiteral("_No failures, partial failures, or errors were recorded for this execution._\n\n");
        return;
    }

    QStringList failedStages;
    for (const auto& stage : m_result.stageResults)
    {
        if (stage.status.compare(QStringLiteral("failed"), Qt::CaseInsensitive) == 0 ||
            stage.status.compare(QStringLiteral("partial"), Qt::CaseInsensitive) == 0 ||
            stage.status.compare(QStringLiteral("cancelled"), Qt::CaseInsensitive) == 0)
        {
            failedStages << stageDisplayName(stage.stageName);
        }
    }

    QStringList failedGroups;
    for (const auto& group : m_result.integrationGroupResults)
    {
        if (!group.success)
        {
            failedGroups << friendlyGroupKey(group.groupKey);
        }
    }

    const QString category = inferLikelyFailureCategory(m_result);
    md += QStringLiteral("<div class=\"an-diagnostic-card\">");
    md += QStringLiteral("<p><strong>Likely Cause Category</strong>: %1</p>").arg(category.toHtmlEscaped());
    md += QStringLiteral("<p><strong>Failed stage(s)</strong>: %1</p>")
              .arg(failedStages.isEmpty() ? QStringLiteral("Not explicitly reported")
                                          : failedStages.join(QStringLiteral(", ")).toHtmlEscaped());
    md += QStringLiteral("<p><strong>Failed group(s)</strong>: %1</p>")
              .arg(failedGroups.isEmpty() ? QStringLiteral("Not explicitly reported")
                                          : failedGroups.join(QStringLiteral(", ")).toHtmlEscaped());
    md += QStringLiteral("<p><strong>Investigation shortcuts</strong>: ");
    md += QStringLiteral("[Pipeline Stages](#pipeline-stages), ");
    md += QStringLiteral("[Warnings & Errors](#warnings-errors), ");
    md += QStringLiteral("[Technical Appendix](#technical-appendix).</p>");
    md += QStringLiteral("</div>\n\n");
}

void WorkflowReportBuilder::appendMetadataCompletenessScore(QString& md) const
{
    appendSectionAnchor(md, QStringLiteral("metadata-completeness"));
    md += QStringLiteral("## \U0001F9EC Metadata Completeness Score\n");
    md += QStringLiteral("_This is metadata/header quality (provenance completeness), not image quality. ");
    md += QStringLiteral("Overall = sum of populated field instances / (light frames \u00D7 tracked fields)._\n\n");

    const InputFileSets fileSets = collectInputFileSets(m_manifest);
    QStringList lightFiles = fileSets.lightFiles.values();
    std::sort(lightFiles.begin(), lightFiles.end(),
              [](const QString& lhs, const QString& rhs) { return lhs.compare(rhs, Qt::CaseInsensitive) < 0; });

    if (lightFiles.isEmpty())
    {
        md += QStringLiteral("_No light frames were available to evaluate metadata completeness._\n\n");
        return;
    }

    struct FieldCompleteness
    {
        QString label;
        int present = 0;
    };

    FieldCompleteness exptime{QStringLiteral("EXPTIME"), 0};
    FieldCompleteness isoGain{QStringLiteral("ISO/Gain"), 0};
    FieldCompleteness offset{QStringLiteral("Offset"), 0};
    FieldCompleteness object{QStringLiteral("OBJECT"), 0};
    FieldCompleteness telescope{QStringLiteral("TELESCOP"), 0};
    FieldCompleteness site{QStringLiteral("Site Coordinates"), 0};

    for (const auto& lightPath : lightFiles)
    {
        // The header alone: a light's pixels are never decoded for its metadata.
        const std::optional<ReportHeader> header = probeReportHeader(m_session, lightPath);
        if (!header.has_value())
        {
            continue;
        }
        const an::ImageMetadata& metadata = header->metadata;

        if (metadataStatesExposure(metadata))
        {
            ++exptime.present;
        }
        if (!formatIsoGainLabel(metadata).isEmpty())
        {
            ++isoGain.present;
        }
        if (!formatOffsetLabel(metadata).isEmpty())
        {
            ++offset.present;
        }
        if (!metadataObjectName(metadata).isEmpty())
        {
            ++object.present;
        }
        if (!metadataTelescope(metadata).isEmpty())
        {
            ++telescope.present;
        }
        if (!formatLocationFromFits(metadata).isEmpty())
        {
            ++site.present;
        }
    }

    const QList<FieldCompleteness> fields = {exptime, isoGain, offset, object, telescope, site};
    const int totalFrames = lightFiles.size();
    int totalPresent = 0;
    for (const auto& field : fields)
    {
        totalPresent += field.present;
    }

    md += QStringLiteral("_Coverage computed across %1 unique light frame(s)._\n\n").arg(totalFrames);
    md += QStringLiteral("| Field | Completeness | Count |\n");
    md += QStringLiteral("|---|---|---|\n");
    for (const auto& field : fields)
    {
        const double pct =
            totalFrames > 0 ? static_cast<double>(field.present) * 100.0 / static_cast<double>(totalFrames) : 0.0;
        const QString ratio = QStringLiteral("%1/%2").arg(field.present).arg(totalFrames);
        md += QStringLiteral("| %1 | <span class=\"an-num\">%2 (%3)</span> | <span class=\"an-num\">%3</span> |\n")
                  .arg(field.label)
                  .arg(formatPercent(pct))
                  .arg(ratio);
    }

    const double overallPct =
        totalFrames > 0 && !fields.isEmpty()
            ? static_cast<double>(totalPresent) * 100.0 / static_cast<double>(totalFrames * fields.size())
            : 0.0;
    const QString overallRatio = QStringLiteral("%1/%2").arg(totalPresent).arg(totalFrames * fields.size());
    md += QStringLiteral(
              "| **Overall** | **<span class=\"an-num\">%1 (%2)</span>** | **<span class=\"an-num\">%2</span>** |\n\n")
              .arg(formatPercent(overallPct))
              .arg(overallRatio);
}

void WorkflowReportBuilder::appendExecutiveSummary(QString& md) const
{
    appendSectionAnchor(md, QStringLiteral("executive-summary"));
    md += QStringLiteral("## \U0001F680 Executive Summary\n");
    md += QStringLiteral("_Interpretation: high-level run KPI snapshot. Integration Groups reports scope outcomes "
                         "(regenerated/reused/failed), ");
    md += QStringLiteral("and Frame Retention uses log-derived Lights/Effective/Registered counts._\n\n");
    md += QStringLiteral("| KPI | Value |\n");
    md += QStringLiteral("|---|---|\n");

    const QString statusStr = compactExecutionStatusWithIcon(m_result);

    QString modeStr = QStringLiteral("Full");
    if (m_manifest.executionMode == ExecutionMode::Incremental)
        modeStr = QStringLiteral("Incremental");
    else if (m_manifest.executionMode == ExecutionMode::RegistrationOnly)
        modeStr = QStringLiteral("Registration Only");

    int totalLightFrames = 0;
    int totalIntegrationMs = 0;
    for (const auto& group : m_manifest.integrationGroups)
    {
        const ExposureBreakdown breakdown = buildExposureBreakdown(group);
        totalLightFrames += breakdown.totalFrames;
        totalIntegrationMs += breakdown.totalMs;
    }

    const GroupOutcomeCounts outcomes = computeGroupOutcomeCounts(m_manifest, m_result);
    const LogInputTotals logInputTotals = parseStackingInputTotalsFromLog(m_result.capturedLog);

    md += QStringLiteral("| Status | %1 |\n").arg(statusStr);
    if (!m_manifest.processLabel.trimmed().isEmpty())
    {
        md += QStringLiteral("| Process | %1 |\n").arg(reportProcessLabel(m_manifest));
    }
    const QString profileText = reportProcessingProfileText(m_manifest);
    if (!profileText.trimmed().isEmpty())
    {
        md += QStringLiteral("| Processing Profile | %1 |\n").arg(profileText);
    }
    md += QStringLiteral("| Execution Mode | %1 |\n").arg(modeStr);
    md += QStringLiteral("| Processing Duration | %1 |\n").arg(formatDuration(m_result.totalDurationMs));
    md += QStringLiteral("| Integration Groups | Total %1, Regenerated %2, Reused %3, Failed %4 |\n")
              .arg(outcomes.totalGroups)
              .arg(outcomes.regeneratedGroups)
              .arg(outcomes.reusedGroups)
              .arg(outcomes.failedGroups);
    md += QStringLiteral("| Light Frames | %1 |\n").arg(totalLightFrames);
    if (totalIntegrationMs > 0)
    {
        md += QStringLiteral("| Total Integration Time | %1 |\n").arg(formatDuration(totalIntegrationMs));
    }
    if (m_result.warningCount > 0 || !m_result.warnings.empty())
    {
        const int warnCount =
            m_result.warningCount > 0 ? m_result.warningCount : static_cast<int>(m_result.warnings.size());
        md += QStringLiteral("| Warnings | %1 |\n").arg(warnCount);
    }
    if (!m_result.errors.empty())
    {
        md += QStringLiteral("| Errors | %1 |\n").arg(m_result.errors.size());
    }
    if (logInputTotals.lights > 0)
    {
        const double effectivePct =
            static_cast<double>(logInputTotals.effective) * 100.0 / static_cast<double>(logInputTotals.lights);
        const double registeredPct =
            static_cast<double>(logInputTotals.registered) * 100.0 / static_cast<double>(logInputTotals.lights);
        const double usableOverEffectivePct =
            logInputTotals.effective > 0
                ? static_cast<double>(logInputTotals.registered) * 100.0 / static_cast<double>(logInputTotals.effective)
                : 0.0;

        md += QStringLiteral("| Frame Retention (Log) | Effective %1 (%2/%3), Registered %4 (%5/%3), Usable/Eff %6 |\n")
                  .arg(formatPercent(effectivePct))
                  .arg(logInputTotals.effective)
                  .arg(logInputTotals.lights)
                  .arg(formatPercent(registeredPct))
                  .arg(logInputTotals.registered)
                  .arg(formatPercent(usableOverEffectivePct));
    }

    md += QStringLiteral("\n");
}

void WorkflowReportBuilder::appendSummary(QString& md) const
{
    appendSectionAnchor(md, QStringLiteral("summary"));
    md += QStringLiteral("## \U0001F4CB Summary\n");
    md += QStringLiteral("_Interpretation: concise run metadata and totals; Total Integration Time is summed from "
                         "per-group exposure metadata, ");
    md += QStringLiteral("and Image Dimensions reflects capture-side group resolution metadata._\n");
    md += QStringLiteral("| Property | Value |\n");
    md += QStringLiteral("|---|---|\n");

    // Status
    const QString statusStr = compactExecutionStatusWithIcon(m_result);
    md += QStringLiteral("| Status | %1 |\n").arg(statusStr);

    if (!m_manifest.processLabel.trimmed().isEmpty())
    {
        md += QStringLiteral("| Process | %1 |\n").arg(reportProcessLabel(m_manifest));
    }
    const QString profileText = reportProcessingProfileText(m_manifest);
    if (!profileText.trimmed().isEmpty())
    {
        md += QStringLiteral("| Processing Profile | %1 |\n").arg(profileText);
    }

    // Mode
    QString modeStr = QStringLiteral("Full");
    if (m_manifest.executionMode == ExecutionMode::Incremental)
        modeStr = QStringLiteral("Incremental");
    else if (m_manifest.executionMode == ExecutionMode::RegistrationOnly)
        modeStr = QStringLiteral("Registration Only");
    md += QStringLiteral("| Execution Mode | %1 |\n").arg(modeStr);

    md += QStringLiteral("| Processing Duration | %1 |\n").arg(formatDuration(m_result.totalDurationMs));

    // Total light frames
    int totalLightFrames = 0;
    for (const auto& group : m_manifest.integrationGroups)
    {
        totalLightFrames += static_cast<int>(group.lightFiles.size());
    }
    md += QStringLiteral("| Light Frames | %1 |\n").arg(totalLightFrames);

    // Total integration time (sum of all group integrations, accounting for mixed exposures)
    int totalIntegrationMs = 0;
    for (const auto& group : m_manifest.integrationGroups)
    {
        const ExposureBreakdown breakdown = buildExposureBreakdown(group);
        totalIntegrationMs += breakdown.totalMs;
    }
    if (totalIntegrationMs > 0)
    {
        md += QStringLiteral("| Total Integration Time | **%1** |\n").arg(formatDuration(totalIntegrationMs));
    }

    // Observing sessions (distinct nights)
    QSet<QDate> observingDates;
    for (const auto& group : m_manifest.integrationGroups)
    {
        for (const auto& lightFile : group.lightFiles)
        {
            const QDate d = extractObservingDate(lightFile.canonicalPath());
            if (d.isValid())
                observingDates.insert(d);
        }
    }
    if (!observingDates.isEmpty())
    {
        md += QStringLiteral("| Observing Sessions | %1 night%2 |\n")
                  .arg(observingDates.size())
                  .arg(observingDates.size() != 1 ? QStringLiteral("s") : QString());
    }

    const GroupOutcomeCounts groupOutcomes = computeGroupOutcomeCounts(m_manifest, m_result);
    if (groupOutcomes.totalGroups > 0)
    {
        md += QStringLiteral("| Integration Groups (Total) | %1 |\n").arg(groupOutcomes.totalGroups);
        md += QStringLiteral("| Regenerated This Run | %1 |\n").arg(groupOutcomes.regeneratedGroups);
        md += QStringLiteral("| Reused Existing Output | %1 |\n").arg(groupOutcomes.reusedGroups);
        md += QStringLiteral("| Failed Groups | %1 |\n").arg(groupOutcomes.failedGroups);
    }

    // Sensor temperature range
    double minTemp = 999.0;
    double maxTemp = -999.0;
    for (const auto& group : m_manifest.integrationGroups)
    {
        for (const auto& lightFile : group.lightFiles)
        {
            const auto temp = extractSensorTemperature(lightFile.canonicalPath());
            if (temp.has_value())
            {
                if (*temp < minTemp)
                    minTemp = *temp;
                if (*temp > maxTemp)
                    maxTemp = *temp;
            }
        }
    }
    if (minTemp <= maxTemp)
    {
        if (qFuzzyCompare(minTemp, maxTemp))
        {
            md += QStringLiteral("| Sensor Temperature | %1 \u00B0C |\n").arg(minTemp, 0, 'f', 1);
        }
        else
        {
            md += QStringLiteral("| Sensor Temperature | %1 to %2 \u00B0C |\n")
                      .arg(minTemp, 0, 'f', 1)
                      .arg(maxTemp, 0, 'f', 1);
        }
    }

    // Image dimensions (first group resolution)
    if (!m_manifest.integrationGroups.empty())
    {
        const QString res = m_manifest.integrationGroups.front().resolution.trimmed();
        if (!res.isEmpty())
        {
            md += QStringLiteral("| Image Dimensions | %1 |\n").arg(friendlyResolution(res));
        }
    }

    if (m_result.warningCount > 0 || !m_result.warnings.empty())
    {
        const int warnCount =
            m_result.warningCount > 0 ? m_result.warningCount : static_cast<int>(m_result.warnings.size());
        md += QStringLiteral("| \u26A0 Warnings | %1 |\n").arg(warnCount);
    }
    if (!m_result.errors.empty())
    {
        md += QStringLiteral("| \u274C Errors | %1 |\n").arg(m_result.errors.size());
    }

    md += QStringLiteral("| Output Directory | `%1` |\n").arg(m_manifest.outputDirectory);
    md += QStringLiteral("\n");
}

void WorkflowReportBuilder::appendAcquisitionSummary(QString& md) const
{
    if (m_manifest.integrationGroups.empty())
        return;

    // Collect per-filter stats.
    struct FilterStats
    {
        QString filter;
        int frameCount = 0;
        int unknownFrames = 0;
        QList<ReportExposureBucket> exposures;
        int totalMs = 0;
    };
    QHash<QString, FilterStats> byFilter;
    int grandTotalFrames = 0;
    int grandTotalMs = 0;

    for (const auto& group : m_manifest.integrationGroups)
    {
        const QString filter = group.filter.trimmed().isEmpty() ? QStringLiteral("Unfiltered") : group.filter.trimmed();

        const ExposureBreakdown breakdown = buildExposureBreakdown(group);

        FilterStats& fs = byFilter[filter];
        fs.filter = filter;
        fs.frameCount += breakdown.totalFrames;
        fs.unknownFrames += breakdown.unknownFrames;
        fs.totalMs += breakdown.totalMs;
        for (const auto& bucket : breakdown.buckets)
        {
            addExposureBucket(fs.exposures, bucket.exposureSeconds, bucket.frameCount);
        }

        grandTotalFrames += breakdown.totalFrames;
        grandTotalMs += breakdown.totalMs;
    }

    if (byFilter.isEmpty() || grandTotalMs <= 0)
        return;

    appendSectionAnchor(md, QStringLiteral("acquisition-summary"));
    md += QStringLiteral("## \U0001F4F7 Acquisition Summary\n\n");
    md += QStringLiteral("_Interpretation: filter-level exposure inventory. Exposures lists frame-count \\u00D7 "
                         "exposure-duration buckets, ");
    md += QStringLiteral(
        "Integration Time is summed exposure time for that filter, and % of Total is filter-time share._\n\n");

    // Exposure breakdown table
    md += QStringLiteral("| Filter | Frames | Exposures | Integration Time | % of Total |\n");
    md += QStringLiteral("|---|---|---|---|---|\n");

    QStringList filterOrder = byFilter.keys();
    filterOrder.sort(Qt::CaseInsensitive);

    for (const auto& key : filterOrder)
    {
        FilterStats fs = byFilter[key];
        std::sort(fs.exposures.begin(), fs.exposures.end(),
                  [](const ReportExposureBucket& lhs, const ReportExposureBucket& rhs) {
                      return lhs.exposureSeconds < rhs.exposureSeconds;
                  });

        const double pct = grandTotalMs > 0 ? (static_cast<double>(fs.totalMs) / grandTotalMs * 100.0) : 0.0;

        // Build integration time string:
        // - single exposure: "3h 37m 0s (217 × 60s)"
        // - mixed exposures: "3h 37m 0s (172 × 60s, 45 × 90s)"
        QString integTimeStr = formatDuration(fs.totalMs);
        if (!fs.exposures.isEmpty())
        {
            integTimeStr += QStringLiteral(" (%1)").arg(formatExposureList(fs.exposures));
        }

        QString exposureStr = fs.exposures.isEmpty() ? QString() : formatExposureList(fs.exposures);
        if (fs.unknownFrames > 0)
        {
            const QString unknownPart = QStringLiteral("%1 \u00D7 ?s").arg(fs.unknownFrames);
            exposureStr = exposureStr.isEmpty() ? unknownPart : QStringLiteral("%1, %2").arg(exposureStr, unknownPart);
        }
        if (exposureStr.isEmpty())
        {
            exposureStr = QStringLiteral("-");
        }

        md += QStringLiteral("| %1 | %2 | %3 | %4 | %5% |\n")
                  .arg(fs.filter)
                  .arg(fs.frameCount)
                  .arg(exposureStr)
                  .arg(integTimeStr)
                  .arg(pct, 0, 'f', 1);
    }

    // Total row
    {
        QString totalIntegStr = formatDuration(grandTotalMs);
        QSet<QString> allExposures;
        QList<ReportExposureBucket> mergedExposureBuckets;
        int mergedUnknownFrames = 0;
        for (const auto& key : filterOrder)
        {
            for (const auto& bucket : byFilter[key].exposures)
            {
                allExposures.insert(formatExposureSeconds(bucket.exposureSeconds));
                addExposureBucket(mergedExposureBuckets, bucket.exposureSeconds, bucket.frameCount);
            }
            mergedUnknownFrames += byFilter[key].unknownFrames;
        }

        if (!mergedExposureBuckets.isEmpty())
        {
            std::sort(mergedExposureBuckets.begin(), mergedExposureBuckets.end(),
                      [](const ReportExposureBucket& lhs, const ReportExposureBucket& rhs) {
                          return lhs.exposureSeconds < rhs.exposureSeconds;
                      });
            if (allExposures.size() == 1 && mergedUnknownFrames == 0)
            {
                totalIntegStr += QStringLiteral(" (%1)").arg(formatExposureList(mergedExposureBuckets));
            }
        }

        md += QStringLiteral("| **Total** | **%1** | | **%2** | |\n").arg(grandTotalFrames).arg(totalIntegStr);
    }
    md += QStringLiteral("\n");

    // Per-session (night) breakdown
    QHash<QDate, QHash<QString, int>> sessionFilterFrames; // date -> filter -> count
    QHash<QDate, QHash<QString, int>> sessionFilterMs; // date -> filter -> integration ms
    for (const auto& group : m_manifest.integrationGroups)
    {
        const QString filter = group.filter.trimmed().isEmpty() ? QStringLiteral("Unfiltered") : group.filter.trimmed();

        const std::optional<double> groupExposure = parseGroupExposure(group);
        const bool hasEmbeddedExposures = group.lightExposureSeconds.size() == group.lightFiles.size();

        for (size_t i = 0; i < group.lightFiles.size(); ++i)
        {
            const auto& lightFile = group.lightFiles[i];
            const QDate d = extractObservingDate(lightFile.canonicalPath());
            if (!d.isValid())
                continue;
            sessionFilterFrames[d][filter]++;

            std::optional<double> expSec;
            if (hasEmbeddedExposures)
            {
                const double embeddedExposure = group.lightExposureSeconds[i];
                if (embeddedExposure > 0.0)
                {
                    expSec = embeddedExposure;
                }
            }
            if (!expSec.has_value())
            {
                expSec = extractExposureFromFilename(lightFile.canonicalPath());
            }
            if (!expSec.has_value() && groupExposure.has_value())
            {
                expSec = groupExposure;
            }
            if (expSec.has_value() && *expSec > 0.0)
            {
                sessionFilterMs[d][filter] += static_cast<int>(std::llround(*expSec * 1000.0));
            }
        }
    }

    if (sessionFilterFrames.size() > 1)
    {
        md += QStringLiteral("### Per-Session Breakdown\n\n");

        QList<QDate> sortedDates = sessionFilterFrames.keys();
        std::sort(sortedDates.begin(), sortedDates.end());

        // Build header: Session | filter1 | filter2 | ... | Total
        md += QStringLiteral("| Session");
        for (const auto& f : filterOrder)
        {
            md += QStringLiteral(" | %1").arg(f);
        }
        md += QStringLiteral(" | Total |\n");

        md += QStringLiteral("|---");
        for (int i = 0; i < filterOrder.size(); ++i)
            md += QStringLiteral("|---");
        md += QStringLiteral("|---|\n");

        for (const QDate& date : sortedDates)
        {
            md += QStringLiteral("| %1").arg(date.toString(QStringLiteral("yyyy-MM-dd")));
            int sessionTotal = 0;
            for (const auto& f : filterOrder)
            {
                const int count = sessionFilterFrames[date].value(f, 0);
                sessionTotal += count;
                if (count > 0)
                {
                    const int ms = sessionFilterMs[date].value(f, 0);
                    if (ms > 0)
                    {
                        md += QStringLiteral(" | %1 (%2)").arg(count).arg(formatDuration(ms));
                    }
                    else
                    {
                        md += QStringLiteral(" | %1").arg(count);
                    }
                }
                else
                {
                    md += QStringLiteral(" | \u2014");
                }
            }
            md += QStringLiteral(" | %1 |\n").arg(sessionTotal);
        }
        md += QStringLiteral("\n");
    }
}

void WorkflowReportBuilder::appendEquipmentUsed(QString& md) const
{
    struct EquipmentRow
    {
        QString group;
        QString target;
        QString camera;
        QString captureResolution;
        QString outputResolution;
        QString isoOrGain;
        QString offset;
        QString telescope;
        QString location;
    };

    QList<EquipmentRow> rows;
    rows.reserve(static_cast<qsizetype>(m_result.integrationGroupResults.size()));

    for (const auto& group : m_result.integrationGroupResults)
    {
        if (!group.success)
        {
            continue;
        }

        QString metadataPath = resolvePathForRead(group.outputPath);
        if (metadataPath.isEmpty() && !group.outputFilename.trimmed().isEmpty())
        {
            const QDir outputDir(m_manifest.outputDirectory);
            metadataPath = resolvePathForRead(outputDir.filePath(group.outputFilename));
        }
        if (metadataPath.isEmpty())
        {
            continue;
        }

        const QFileInfo metadataInfo(metadataPath);
        if (!metadataInfo.exists() || !metadataInfo.isFile())
        {
            continue;
        }

        // The master's header alone: its pixels are never decoded for its metadata.
        const std::optional<ReportHeader> header = probeReportHeader(m_session, metadataPath);
        if (!header.has_value())
        {
            continue;
        }
        const an::ImageMetadata& metadata = header->metadata;

        const WorkflowManifest::IntegrationGroup* manifestGroupDef = nullptr;
        QString manifestTarget;
        for (const auto& manifestGroup : m_manifest.integrationGroups)
        {
            if (manifestGroup.groupKey.toString() == group.groupKey)
            {
                manifestGroupDef = &manifestGroup;
                manifestTarget = manifestGroup.object.trimmed();
                break;
            }
        }

        EquipmentRow row;
        row.group = friendlyGroupKey(group.groupKey);
        if (row.group.trimmed().isEmpty())
        {
            row.group = group.outputFilename.trimmed();
        }
        if (row.group.trimmed().isEmpty())
        {
            row.group = QFileInfo(metadataPath).fileName();
        }

        if (header->width > 0 && header->height > 0)
        {
            row.outputResolution = QStringLiteral("%1 \u00D7 %2")
                                       .arg(static_cast<qulonglong>(header->width))
                                       .arg(static_cast<qulonglong>(header->height));
        }
        if (manifestGroupDef != nullptr)
        {
            const auto captureDims = parseResolutionDimensions(manifestGroupDef->resolution);
            if (captureDims.has_value())
            {
                row.captureResolution = QStringLiteral("%1 \u00D7 %2").arg(captureDims->first).arg(captureDims->second);
            }
            else if (!manifestGroupDef->resolution.trimmed().isEmpty())
            {
                row.captureResolution = friendlyResolution(manifestGroupDef->resolution.trimmed());
            }
        }

        row.target = metadataObjectName(metadata);
        if (row.target.isEmpty())
        {
            row.target = manifestTarget;
        }

        row.camera = metadataCameraModel(metadata);
        row.isoOrGain = formatIsoGainLabel(metadata);
        row.offset = formatOffsetLabel(metadata);
        row.telescope = metadataTelescope(metadata);
        row.location = formatLocationFromFits(metadata);

        const bool hasEquipmentData = !row.camera.isEmpty() || !row.isoOrGain.isEmpty() || !row.offset.isEmpty() ||
                                      !row.telescope.isEmpty() || !row.location.isEmpty();
        if (!hasEquipmentData)
        {
            continue;
        }

        rows.push_back(std::move(row));
    }

    if (rows.isEmpty())
    {
        return;
    }

    struct EquipmentAggregate
    {
        QStringList groups;
        EquipmentRow row;
    };

    const auto makeKey = [](const EquipmentRow& row) {
        QStringList parts;
        parts.reserve(8);
        parts << row.target.trimmed();
        parts << row.camera.trimmed();
        parts << row.captureResolution.trimmed();
        parts << row.outputResolution.trimmed();
        parts << row.isoOrGain.trimmed();
        parts << row.offset.trimmed();
        parts << row.telescope.trimmed();
        parts << row.location.trimmed();
        return parts.join(QChar(0x1F));
    };

    QList<EquipmentAggregate> aggregates;
    aggregates.reserve(rows.size());
    QHash<QString, int> aggregateByKey;
    aggregateByKey.reserve(rows.size());

    for (const auto& row : rows)
    {
        const QString key = makeKey(row);
        const auto it = aggregateByKey.constFind(key);
        if (it == aggregateByKey.constEnd())
        {
            EquipmentAggregate agg;
            agg.groups = {row.group};
            agg.row = row;
            aggregates.push_back(std::move(agg));
            aggregateByKey.insert(key, aggregates.size() - 1);
            continue;
        }

        EquipmentAggregate& agg = aggregates[*it];
        agg.groups.push_back(row.group);
    }

    appendSectionAnchor(md, QStringLiteral("equipment-used"));
    md += QStringLiteral("## \U0001F52D Equipment Used\n\n");
    md += QStringLiteral("_Values in this section are extracted from FITS headers of generated master outputs._\n\n");
    md += QStringLiteral("_Capture Resolution comes from grouping/capture metadata. ");
    md += QStringLiteral("Output Resolution is the final stacked master size (can be smaller after registration crop ");
    md += QStringLiteral("or larger when drizzle is enabled)._ \n\n");

    md += QStringLiteral("| Integration Group | Target | Camera | Capture Resolution | Output Resolution | ISO/Gain | "
                         "Offset | Telescope | Location (GPS) |\n");
    md += QStringLiteral("|---|---|---|---|---|---|---|---|---|\n");

    const auto tableCell = [](QString value) {
        value = value.trimmed();
        if (value.isEmpty())
        {
            return QStringLiteral("-");
        }
        value.replace(QLatin1Char('\n'), QLatin1Char(' '));
        value.replace(QLatin1Char('|'), QStringLiteral("\\|"));
        return value;
    };

    for (auto& agg : aggregates)
    {
        agg.groups.removeDuplicates();
        std::sort(agg.groups.begin(), agg.groups.end(),
                  [](const QString& lhs, const QString& rhs) { return lhs.compare(rhs, Qt::CaseInsensitive) < 0; });
        EquipmentRow& row = agg.row;
        row.group = agg.groups.join(QStringLiteral(", "));

        md += QStringLiteral("| %1 | %2 | %3 | %4 | %5 | %6 | %7 | %8 | %9 |\n")
                  .arg(tableCell(row.group))
                  .arg(tableCell(row.target))
                  .arg(tableCell(row.camera))
                  .arg(tableCell(row.captureResolution))
                  .arg(tableCell(row.outputResolution))
                  .arg(tableCell(row.isoOrGain))
                  .arg(tableCell(row.offset))
                  .arg(tableCell(row.telescope))
                  .arg(tableCell(row.location));
    }

    md += QStringLiteral("\n");
}

void WorkflowReportBuilder::appendRunFocus(QString& md) const
{
    appendSectionAnchor(md, QStringLiteral("run-focus"));
    md += QStringLiteral("## \U0001F3AF Run Focus\n");

    const QString scope =
        m_manifest.executionScope.trimmed().isEmpty() ? QStringLiteral("full") : m_manifest.executionScope.trimmed();
    md += QStringLiteral("- **Scope**: %1").arg(scope);

    if (!m_manifest.executionScopeKey.trimmed().isEmpty())
    {
        md += QStringLiteral(" (`%1`)").arg(friendlyGroupKey(m_manifest.executionScopeKey));
    }
    md += QStringLiteral("\n");

    if (m_manifest.executionMode == ExecutionMode::Incremental)
    {
        const int changedIntegrationGroups = static_cast<int>(m_manifest.incrementalIntegrationGroups.size());
        const int totalIntegrationGroups = static_cast<int>(m_manifest.integrationGroups.size());
        md += QStringLiteral("- **Integration Groups Updated**: %1 / %2\n")
                  .arg(changedIntegrationGroups)
                  .arg(totalIntegrationGroups);

        if (changedIntegrationGroups > 0)
        {
            md += QStringLiteral("\n<details>\n<summary>Changed Integration Groups (%1)</summary>\n\n")
                      .arg(changedIntegrationGroups);
            for (const auto& groupKey : m_manifest.incrementalIntegrationGroups)
            {
                md += QStringLiteral("- `%1`\n").arg(friendlyGroupKey(groupKey.toString()));
            }
            md += QStringLiteral("\n</details>\n\n");
        }
    }

    const GroupOutcomeCounts groupOutcomes = computeGroupOutcomeCounts(m_manifest, m_result);
    if (groupOutcomes.totalGroups > 0)
    {
        md += QStringLiteral("- **Group Outcomes**: Total %1 | Regenerated %2 | Reused %3 | Failed %4\n")
                  .arg(groupOutcomes.totalGroups)
                  .arg(groupOutcomes.regeneratedGroups)
                  .arg(groupOutcomes.reusedGroups)
                  .arg(groupOutcomes.failedGroups);
    }

    if (!m_result.stageResults.empty())
    {
        QStringList executedStages;
        for (const auto& stage : m_result.stageResults)
        {
            executedStages.append(stageDisplayName(stage.stageName));
        }
        md += QStringLiteral("- **Executed Stages**: %1\n").arg(executedStages.join(QStringLiteral(", ")));
    }

    md += QStringLiteral("\n");
}

void WorkflowReportBuilder::appendWorkflowTiming(QString& md) const
{
    appendSectionAnchor(md, QStringLiteral("workflow-timing"));
    md += QStringLiteral("## ⏱️ Workflow Timing\n");

    if (!m_result.executorTiming.captured)
    {
        if (!m_result.preparationTiming.hasMeasurements())
        {
            md += QStringLiteral(
                "_Preparation and exclusive executor-tail timing were not supplied by this execution path. "
                "Processing Duration is the executor's whole interval._\n\n");
            return;
        }

        md += QStringLiteral(
            "_Manifest Build and Plan & Validation are exclusive sibling phases. Preparation Total is derived from "
            "those children and must not be added to them. Executor Processing is the executor's whole interval; "
            "exclusive executor-tail timing was not supplied._\n\n");
        md += QStringLiteral("| Phase | Duration |\n");
        md += QStringLiteral("|---|---|\n");
        md +=
            QStringLiteral("| Manifest Build | %1 |\n").arg(formatDuration(m_result.preparationTiming.manifestBuildMs));
        md += QStringLiteral("| Plan & Validation | %1 |\n")
                  .arg(formatDuration(m_result.preparationTiming.planAndValidationMs));
        md += QStringLiteral("| Preparation Total (derived) | %1 |\n")
                  .arg(formatDuration(m_result.preparationTiming.totalMs()));
        md += QStringLiteral("| Executor Processing | %1 |\n\n").arg(formatDuration(m_result.totalDurationMs));
        return;
    }

    const bool defer_timing = m_reportContext.value(QStringLiteral("deferWorkflowTiming")).toBool();
    const auto timingValue = [&](const char* token, const qint64 value) {
        return defer_timing ? QString::fromLatin1(token) : formatDuration(value);
    };

    md +=
        QStringLiteral("_Stage Execution, State Commit, Archive, and Report are exclusive sibling phases. Executor "
                       "Total is their parent interval, and parent totals must not be added to their children. A "
                       "skipped phase is represented as 0ms; an applicable interval may also round down to 0ms._\n\n");
    md += QStringLiteral("| Phase | Duration |\n");
    md += QStringLiteral("|---|---|\n");
    if (m_result.preparationTiming.hasMeasurements())
    {
        md +=
            QStringLiteral("| Manifest Build | %1 |\n").arg(formatDuration(m_result.preparationTiming.manifestBuildMs));
        md += QStringLiteral("| Plan & Validation | %1 |\n")
                  .arg(formatDuration(m_result.preparationTiming.planAndValidationMs));
        md += QStringLiteral("| Preparation Total (derived) | %1 |\n")
                  .arg(formatDuration(m_result.preparationTiming.totalMs()));
    }
    md += QStringLiteral("| Executor Total (parent) | %1 |\n")
              .arg(timingValue(kExecutorTotalTimingToken, m_result.totalDurationMs));
    md += QStringLiteral("| Stage Execution | %1 |\n")
              .arg(timingValue(kStageExecutionTimingToken, m_result.executorTiming.stageExecutionMs));
    md += QStringLiteral("| State Commit | %1 |\n")
              .arg(timingValue(kStateCommitTimingToken, m_result.executorTiming.stateCommitMs));
    md += QStringLiteral("| Archive | %1 |\n").arg(timingValue(kArchiveTimingToken, m_result.executorTiming.archiveMs));
    md += QStringLiteral("| Report | %1 |\n").arg(timingValue(kReportTimingToken, m_result.executorTiming.reportMs));
    if (m_result.preparationTiming.hasMeasurements())
    {
        md += QStringLiteral("| Start to Ready (derived) | %1 |\n")
                  .arg(timingValue(kStartToReadyTimingToken,
                                   m_result.preparationTiming.totalMs() + m_result.totalDurationMs));
    }
    md += QStringLiteral("\n");
}

void WorkflowReportBuilder::appendWorkflowCounters(QString& md) const
{
    std::optional<WorkflowExecutionCounters> counters = m_result.executionCounters;
    if (!counters.has_value() && m_counters != nullptr)
    {
        counters = m_counters->snapshot();
    }
    if (!counters.has_value())
    {
        return;
    }

    const bool defer_counters = m_counters != nullptr;
    const auto counterValue = [&](const char* token, const std::uint64_t value) {
        return defer_counters ? QString::fromLatin1(token) : QString::number(value);
    };

    md += QStringLiteral("### Execution Counters\n\n");
    md += QStringLiteral(
        "_Optional diagnostics only; these values do not control workflow execution or scientific results._\n\n");
    md += QStringLiteral("| Counter | Value |\n");
    md += QStringLiteral("|---|---:|\n");
    md +=
        QStringLiteral("| Bytes hashed | %1 |\n").arg(counterValue(kBytesHashedCounterToken, counters->bytesHashed()));
    md +=
        QStringLiteral("| Bytes copied | %1 |\n").arg(counterValue(kBytesCopiedCounterToken, counters->bytesCopied()));
    md += QStringLiteral("| SQLite statement executions | %1 |\n")
              .arg(counterValue(kSqliteStatementsCounterToken, counters->sqliteStatementExecutions()));
    md += QStringLiteral("| SQLite transactions started | %1 |\n")
              .arg(counterValue(kSqliteTransactionsStartedCounterToken, counters->sqliteTransactionsStarted()));
    md += QStringLiteral("| SQLite transactions committed | %1 |\n")
              .arg(counterValue(kSqliteTransactionsCommittedCounterToken, counters->sqliteTransactionsCommitted()));
    md += QStringLiteral("| SQLite transactions rolled back | %1 |\n")
              .arg(counterValue(kSqliteTransactionsRolledBackCounterToken, counters->sqliteTransactionsRolledBack()));
    md += QStringLiteral("| Input decode operations | %1 |\n")
              .arg(counterValue(kInputDecodeOperationsCounterToken, counters->inputDecodeOperations()));
    md += QStringLiteral("| Scientific output encode completions | %1 |\n")
              .arg(counterValue(kScientificOutputEncodeCompletionsCounterToken,
                                counters->scientificOutputEncodeCompletions()));
    md += QStringLiteral("| Input passes started | %1 |\n")
              .arg(counterValue(kInputPassesStartedCounterToken, counters->inputPassesStarted()));
    md += QStringLiteral("| Tile passes started | %1 |\n")
              .arg(counterValue(kTilePassesStartedCounterToken, counters->tilePassesStarted()));
    md += QStringLiteral("| Stack loader requests | %1 |\n")
              .arg(counterValue(kStackLoaderRequestsCounterToken, counters->stackLoaderRequests()));
    md += QStringLiteral("| ROI subset reads | %1 |\n")
              .arg(counterValue(kRoiSubsetReadsCounterToken, counters->roiSubsetReads()));
    md += QStringLiteral("| Full-frame fallback reads | %1 |\n")
              .arg(counterValue(kFullFrameFallbackReadsCounterToken, counters->fullFrameFallbackReads()));
    md += QStringLiteral("| Logical decoded pixel bytes | %1 |\n")
              .arg(counterValue(kLogicalDecodedPixelBytesCounterToken, counters->logicalDecodedPixelBytes()));
    md += QStringLiteral("| Calibration invocations | %1 |\n")
              .arg(counterValue(kCalibrationInvocationsCounterToken, counters->calibrationInvocations()));
    md += QStringLiteral("| Calibrated spill files created | %1 |\n")
              .arg(counterValue(kCalibratedSpillFilesCreatedCounterToken, counters->calibratedSpillFilesCreated()));
    md += QStringLiteral("| Calibrated spill payload bytes written | %1 |\n")
              .arg(counterValue(kCalibratedSpillBytesWrittenCounterToken, counters->calibratedSpillBytesWritten()));
    md += QStringLiteral("| Calibrated spill payload bytes read | %1 |\n")
              .arg(counterValue(kCalibratedSpillBytesReadCounterToken, counters->calibratedSpillBytesRead()));
    md += QStringLiteral("| Calibrated spill ROI reads | %1 |\n")
              .arg(counterValue(kCalibratedSpillRoiReadsCounterToken, counters->calibratedSpillRoiReads()));
    md += QStringLiteral("| Calibrated spill cleanup successes | %1 |\n")
              .arg(counterValue(kCalibratedSpillCleanupSuccessesCounterToken,
                                counters->calibratedSpillCleanupSuccesses()));
    md +=
        QStringLiteral("| Calibrated spill cleanup failures | %1 |\n")
            .arg(counterValue(kCalibratedSpillCleanupFailuresCounterToken, counters->calibratedSpillCleanupFailures()));
    md += QStringLiteral("| Dump payload bytes read | %1 |\n")
              .arg(counterValue(kDumpBytesReadCounterToken, counters->dumpBytesRead()));
    md += QStringLiteral("| Dump payload bytes written | %1 |\n")
              .arg(counterValue(kDumpBytesWrittenCounterToken, counters->dumpBytesWritten()));
    md += QStringLiteral("| Dense pixmap payload bytes read | %1 |\n")
              .arg(counterValue(kPixmapBytesReadCounterToken, counters->pixmapBytesRead()));
    md += QStringLiteral("| Dense pixmap payload bytes written | %1 |\n\n")
              .arg(counterValue(kPixmapBytesWrittenCounterToken, counters->pixmapBytesWritten()));
}

void WorkflowReportBuilder::appendPipelineStages(QString& md) const
{
    if (m_result.stageResults.empty())
        return;

    const QHash<QString, int> logStageGroups = logStageGroupCountMap(m_result.capturedLog);

    appendSectionAnchor(md, QStringLiteral("pipeline-stages"));
    md += QStringLiteral("## \u2699\uFE0F Pipeline Stages\n");
    md += QStringLiteral("_Interpretation: stage-level execution timeline. Groups indicates affected "
                         "integration/calibration scope count for each stage._\n");
    md += QStringLiteral("| # | Stage | Status | Duration | Groups |\n");
    md += QStringLiteral("|---|---|---|---|---|\n");

    int stageNum = 0;
    for (const auto& stage : m_result.stageResults)
    {
        ++stageNum;

        QString statusIcon = "\u2705"; // green checkmark
        if (stage.status == "failed")
            statusIcon = "\u274C";
        else if (stage.status == "partial")
            statusIcon = "\u26A0\uFE0F";
        else if (stage.status == "cancelled")
            statusIcon = "\u21BA";
        else if (stage.status == "unknown")
            statusIcon = "\u2753";

        const QString niceName = stageDisplayName(stage.stageName);

        // Groups are shown as scope counts for consistency across report sections.
        int groupCount = inferGroupCountForStage(stage.stageName);
        if (groupCount <= 0 && stage.groupsProcessed > 0)
        {
            groupCount = stage.groupsProcessed;
        }
        const QString stageKey = canonicalStageKey(stage.stageName);
        if (logStageGroups.contains(stageKey))
        {
            groupCount = logStageGroups.value(stageKey);
        }
        const QString groupsVal = QString::number(groupCount);
        const QString severityClass = stageSeverityClass(stage.status);
        const QString severityLabel =
            stage.status.trimmed().isEmpty() ? QStringLiteral("unknown") : stage.status.trimmed().toLower();
        const QString statusHtml = QStringLiteral("<span class=\"an-severity-pill %1\">%2 %3</span>")
                                       .arg(severityClass, statusIcon.toHtmlEscaped(), severityLabel.toHtmlEscaped());

        md += QStringLiteral("| %1 | %2 | %3 | %4 | %5 |\n")
                  .arg(stageNum)
                  .arg(niceName)
                  .arg(statusHtml)
                  .arg(formatDuration(stage.durationMs))
                  .arg(groupsVal);
    }
    md += QStringLiteral("\n");
}

} // namespace ImageIntegration::Reporting
