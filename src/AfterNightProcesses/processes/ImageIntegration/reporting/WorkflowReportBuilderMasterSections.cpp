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
 * @file WorkflowReportBuilderMasterSections.cpp
 * @brief Stacked-master, calibration/registration, parameter and log sections of the workflow report.
 *
 * Split out of WorkflowReportBuilder.cpp to keep that unit readable.
 */

namespace ImageIntegration::Reporting
{

void WorkflowReportBuilder::appendStackedMasters(QString& md) const
{
    appendSectionAnchor(md, QStringLiteral("stacked-masters"));
    md += QStringLiteral("## \u2B50 Stacked Masters\n\n");
    md += QStringLiteral("_Quality statistics in this section are computed from the unstretched master FITS outputs "
                         "(not from preview images)._\n\n");

    if (m_result.integrationGroupResults.empty())
    {
        if (m_manifest.executionMode == ExecutionMode::Incremental)
        {
            md += QStringLiteral("_No stacked masters were generated in this incremental run because all integration "
                                 "groups are up to date._\n\n");
        }
        else
        {
            md += QStringLiteral("_No stacked masters were generated in this run._\n\n");
        }
        return;
    }

    // Build calibration lookup: light path -> calibration params
    QHash<QString, const WorkflowManifest::LightsCalibrationParams*> calibByLight;
    for (const auto& lc : m_manifest.lightsCalibration)
    {
        calibByLight.insert(lc.lightPath.canonicalPath(), &lc);
    }

    const QString metricsDbPath = registrationMetricsDatabasePath(m_manifest, m_reportBaseDir);

    for (const auto& group : m_result.integrationGroupResults)
    {
        if (!group.success)
            continue;

        md += QStringLiteral("### %1\n").arg(friendlyGroupKey(group.groupKey));
        md += QStringLiteral("- **Output**: %1 (%2)\n").arg(group.outputFilename, formatFileSize(group.outputBytes));
        if (group.outputRetentionStatus == OutputRetentionStatus::NotRetainedByPolicy ||
            group.outputRetentionStatus == OutputRetentionStatus::NotRetainedByArchiveFailure)
        {
            const QString reason = group.outputRetentionReason.trimmed();
            md += QStringLiteral("- **History Retention**: Scientific output succeeded, but its history copy was not "
                                 "retained%1.\n")
                      .arg(reason.isEmpty() ? QString() : QStringLiteral(" because %1").arg(reason));
        }
        md += QStringLiteral("- **Light Frames**: %1\n").arg(group.lightFrameCount);

        // Find corresponding IntegrationGroup definition for settings/exposure
        const WorkflowManifest::IntegrationGroup* def = nullptr;
        for (const auto& g : m_manifest.integrationGroups)
        {
            if (g.groupKey.toString() == group.groupKey)
            {
                def = &g;
                break;
            }
        }

        // Integration time
        if (def)
        {
            const ExposureBreakdown breakdown = buildExposureBreakdown(*def);
            if (breakdown.totalMs > 0 && breakdown.knownFrames > 0)
            {
                QString integrationDetails;
                if (breakdown.buckets.size() == 1 && breakdown.unknownFrames == 0)
                {
                    integrationDetails = formatExposureList(breakdown.buckets);
                }
                else
                {
                    integrationDetails = formatExposureList(breakdown.buckets);
                    if (breakdown.unknownFrames > 0)
                    {
                        integrationDetails += QStringLiteral(", %1 \u00D7 ?s").arg(breakdown.unknownFrames);
                    }
                }

                md += QStringLiteral("- **Integration Time**: %1 (%2)\n")
                          .arg(formatDuration(breakdown.totalMs))
                          .arg(integrationDetails);
            }
        }

        // Calibration applied
        if (def && !calibByLight.isEmpty())
        {
            QSet<QString> biasUsed, darkUsed, flatUsed;
            for (const auto& lightFile : def->lightFiles)
            {
                auto it = calibByLight.find(lightFile.canonicalPath());
                if (it != calibByLight.end())
                {
                    if ((*it)->biasPath.has_value())
                        biasUsed.insert(QFileInfo((*it)->biasPath->canonicalPath()).fileName());
                    if ((*it)->darkPath.has_value())
                        darkUsed.insert(QFileInfo((*it)->darkPath->canonicalPath()).fileName());
                    if ((*it)->flatPath.has_value())
                        flatUsed.insert(QFileInfo((*it)->flatPath->canonicalPath()).fileName());
                }
            }
            QStringList calibParts;
            if (!biasUsed.isEmpty())
                calibParts << QStringLiteral("Bias: %1").arg(biasUsed.values().join(QStringLiteral(", ")));
            if (!darkUsed.isEmpty())
                calibParts << QStringLiteral("Dark: %1").arg(darkUsed.values().join(QStringLiteral(", ")));
            if (!flatUsed.isEmpty())
                calibParts << QStringLiteral("Flat: %1").arg(flatUsed.values().join(QStringLiteral(", ")));
            if (!calibParts.isEmpty())
            {
                md += QStringLiteral("- **Calibration**: %1\n").arg(calibParts.join(QStringLiteral(" | ")));
            }
        }

        if (!group.registrationGroupKey.isEmpty())
        {
            md += QStringLiteral("- **Registration Group**: %1\n").arg(friendlyGroupKey(group.registrationGroupKey));
        }
        if (!group.registrationInputPolicy.trimmed().isEmpty())
        {
            md += QStringLiteral("- **Registration Input Policy**: %1\n").arg(group.registrationInputPolicy);
        }
        if (group.masterSolution != MasterSolutionOutcome::NotRecorded)
        {
            const QString outcome = group.masterSolution == MasterSolutionOutcome::Propagated
                                        ? QStringLiteral("Propagated")
                                    : group.masterSolution == MasterSolutionOutcome::Solved ? QStringLiteral("Solved")
                                                                                            : QStringLiteral("None");
            md += QStringLiteral("- **Master Solution**: %1%2\n")
                      .arg(outcome, group.masterSolutionDetail.isEmpty()
                                        ? QString()
                                        : QStringLiteral(" - %1").arg(group.masterSolutionDetail));
        }

        if (def)
        {
            md += QStringLiteral("- **Stacking**: %1\n")
                      .arg(stage_utils::describeStackingSettings(def->stacking,
                                                                 def->drizzle.has_value() && def->drizzle->enabled));
            if (def->drizzle.has_value() && def->drizzle->enabled)
            {
                md += QStringLiteral("- **Drizzle**: %1\n")
                          .arg(stage_utils::describeDrizzleSettings(def->drizzle.value()));
            }
        }

        if (group.finalizationTiming.captured)
        {
            md += QStringLiteral("- Duration (parent): %1\n").arg(formatDuration(group.durationMs));
            md += QStringLiteral(
                "\n#### Stack Group Finalization Timing\n\n"
                "These are exclusive child phases included in the group parent duration; they must not be added to "
                "that parent when calculating report totals.\n\n"
                "| Finalization Phase | Status | Duration |\n"
                "|---|---|---:|\n");
            for (const auto& phase : group.finalizationTiming.phases)
            {
                md += QStringLiteral("| %1 | %2 | %3 |\n")
                          .arg(phase.stageName, phase.status, formatDuration(phase.durationMs));
            }
            md += QStringLiteral("\n");
        }
        else
        {
            md += QStringLiteral("- **Duration**: %1\n").arg(formatDuration(group.durationMs));
        }

        QString rawStatsPath = resolvePathForRead(group.outputPath);
        if (rawStatsPath.isEmpty() && !group.outputFilename.trimmed().isEmpty())
        {
            const QDir outputDir(m_manifest.outputDirectory);
            rawStatsPath = resolvePathForRead(outputDir.filePath(group.outputFilename));
        }
        const auto stats = analyzeRawImageCached(rawStatsPath);

        if (def && stats.has_value())
        {
            const auto sourceDims = parseResolutionDimensions(def->resolution);
            if (sourceDims.has_value())
            {
                const double sourceArea =
                    static_cast<double>(sourceDims->first) * static_cast<double>(sourceDims->second);
                const double outputArea = static_cast<double>(stats->width) * static_cast<double>(stats->height);
                if (sourceArea > 0.0 && outputArea > 0.0)
                {
                    const double retainedPct = std::clamp(outputArea * 100.0 / sourceArea, 0.0, 100.0);
                    const double lostPct = (std::max)(0.0, 100.0 - retainedPct);
                    md += QStringLiteral("- **Registration Overlap Retention**: %1 (loss %2)\n")
                              .arg(formatPercent(retainedPct))
                              .arg(formatPercent(lostPct));
                }
            }
        }

        if (stats.has_value())
        {
            const auto meanOpt =
                averageChannelStat(stats->channelStats, [](const RawChannelStats& c) { return c.mean; });
            const auto bgNoiseOpt =
                averageChannelStat(stats->channelStats, [](const RawChannelStats& c) { return c.backgroundNoise; });
            const auto minOpt =
                averageChannelStat(stats->channelStats, [](const RawChannelStats& c) { return c.minimum; });
            const auto maxOpt =
                averageChannelStat(stats->channelStats, [](const RawChannelStats& c) { return c.maximum; });

            QStringList qualityParts;
            if (meanOpt.has_value())
            {
                qualityParts << QStringLiteral("Mean %1").arg(formatStatValue(*meanOpt));
            }
            if (bgNoiseOpt.has_value())
            {
                qualityParts << QStringLiteral("BG Noise %1").arg(formatStatValue(*bgNoiseOpt));
            }
            if (meanOpt.has_value() && bgNoiseOpt.has_value() && *bgNoiseOpt > 0.0)
            {
                qualityParts << QStringLiteral("SNR proxy %1").arg(formatSnrProxyValue(*meanOpt / *bgNoiseOpt));
            }
            if (minOpt.has_value() && maxOpt.has_value())
            {
                qualityParts << QStringLiteral("Dynamic Range %1").arg(formatStatValue(*maxOpt - *minOpt));
            }
            if (!qualityParts.isEmpty())
            {
                md += QStringLiteral("- **Quality Snapshot**: %1\n").arg(qualityParts.join(QStringLiteral(" | ")));
            }
        }

        if (group.qualityReport.valid)
        {
            const auto& quality = group.qualityReport;
            const qulonglong inputFrameCount = quality.inputFrameCount > 0
                                                   ? static_cast<qulonglong>(quality.inputFrameCount)
                                                   : static_cast<qulonglong>((std::max)(group.lightFrameCount, 0));

            md += QStringLiteral("\n#### Stacking Quality Report\n");
            md += QStringLiteral("- **Effective Frames**: %1 / %2\n")
                      .arg(formatCompactDouble(quality.effectiveFrameCount))
                      .arg(inputFrameCount);
            md += QStringLiteral("- **Actual SNR Gain**: %1x\n").arg(formatCompactDouble(quality.actualSnrGain));
            md += QStringLiteral("- **Theoretical SNR Gain**: %1x\n")
                      .arg(formatCompactDouble(quality.theoreticalSnrGain));
            md += QStringLiteral("- **Integration Efficiency**: %1\n")
                      .arg(formatPercent(quality.integrationEfficiency * 100.0));
            md += QStringLiteral("- **Coverage**: min %1 | median %2 | max %3 | stdev %4\n")
                      .arg(formatCompactDouble(quality.minCoverage))
                      .arg(formatCompactDouble(quality.medianCoverage))
                      .arg(formatCompactDouble(quality.maxCoverage))
                      .arg(formatCompactDouble(quality.coverageStddev));
            if (!quality.normalization.isEmpty())
            {
                md += QStringLiteral("- **Normalization**: %1\n").arg(quality.normalization.toHtmlEscaped());
            }

            if (quality.hasRejectionStats)
            {
                md += QStringLiteral("- **Global Rejection Rate**: %1\n")
                          .arg(formatPercent(quality.globalRejectionRate * 100.0));
                md += QStringLiteral("- **Per-Frame Rejection Rate**: min %1 | mean %2 | median %3 | max %4\n")
                          .arg(formatPercent(quality.minFrameRejectionRate * 100.0))
                          .arg(formatPercent(quality.meanFrameRejectionRate * 100.0))
                          .arg(formatPercent(quality.medianFrameRejectionRate * 100.0))
                          .arg(formatPercent(quality.maxFrameRejectionRate * 100.0));
            }

            if (quality.hasWeightingStats)
            {
                md += QStringLiteral("- **Weight Range**: %1 to %2\n")
                          .arg(formatCompactDouble(quality.weightMin))
                          .arg(formatCompactDouble(quality.weightMax));
                md += QStringLiteral("- **Weight Stddev**: %1\n").arg(formatCompactDouble(quality.weightStddev));
                md += QStringLiteral("- **Weight Efficiency**: %1\n")
                          .arg(formatPercent(quality.weightEfficiency * 100.0));
            }

            if (quality.hasLocalWeightingStats)
            {
                md += QStringLiteral("\n**Per-Pixel (Local) Weighting**\n\n");
                md += QStringLiteral("- **Window Size**: %1 | **Grid Spacing**: %2 px | **SNR Exponent**: %3\n")
                          .arg(quality.localWeightWindowSize)
                          .arg(quality.localWeightGridSpacing)
                          .arg(formatCompactDouble(quality.localWeightSnrExponent));
                md += QStringLiteral("- **Grids**: %1 frames\n").arg(quality.localWeightGridCount);
                md += QStringLiteral("- **Node Weight Range**: %1 to %2 | mean %3 | stddev %4\n")
                          .arg(formatCompactDouble(quality.localWeightMin))
                          .arg(formatCompactDouble(quality.localWeightMax))
                          .arg(formatCompactDouble(quality.localWeightMean))
                          .arg(formatCompactDouble(quality.localWeightStddev));
            }

            if (quality.hasSpatialRejectionClusters)
            {
                md += QStringLiteral("\n**Spatial Rejection Clusters**\n\n");
                md += QStringLiteral("- **Detected Clusters**: %1\n")
                          .arg(static_cast<qulonglong>(quality.spatialClusterCount));
                md += QStringLiteral("- **Clustered Pixels**: %1 (%2)\n")
                          .arg(static_cast<qulonglong>(quality.spatialClusterPixels))
                          .arg(formatPercent(quality.spatialClusterFraction * 100.0));
                md += QStringLiteral("- **Largest Cluster**: %1 px (%2)\n")
                          .arg(static_cast<qulonglong>(quality.spatialLargestClusterPixels))
                          .arg(formatPercent(quality.spatialLargestClusterFraction * 100.0));

                if (!quality.spatialClusters.empty())
                {
                    const auto& largest = quality.spatialClusters.front();
                    md += QStringLiteral("- **Largest Cluster Bounds**: %1 map | (%2, %3) → (%4, %5)\n")
                              .arg(rejectionClusterMapKindToString(largest.mapKind))
                              .arg(largest.minX)
                              .arg(largest.minY)
                              .arg(largest.maxX)
                              .arg(largest.maxY);
                }
            }

            if (quality.spatialRestackRequested)
            {
                md +=
                    QStringLiteral("- **Spatial Re-Stack**: %1\n")
                        .arg(quality.spatialRestackApplied ? QStringLiteral("Applied") : QStringLiteral("Not applied"));
                md += QStringLiteral("- **Flagged Frame Threshold**: %1\n")
                          .arg(formatPercent(quality.spatialRestackFlaggedFrameThreshold * 100.0));

                if (!quality.spatialRestackExcludedFrameIndices.empty() && def)
                {
                    QStringList flaggedFrames;
                    for (int i = 0; i < static_cast<int>(quality.spatialRestackExcludedFrameIndices.size()); ++i)
                    {
                        const size_t index = quality.spatialRestackExcludedFrameIndices[static_cast<size_t>(i)];
                        QString label = QStringLiteral("#%1").arg(static_cast<qulonglong>(index + 1));
                        if (index < def->lightFiles.size())
                        {
                            label = QFileInfo(def->lightFiles[index].canonicalPath()).fileName();
                        }

                        if (static_cast<size_t>(i) < quality.spatialRestackExcludedFrameRates.size())
                        {
                            label += QStringLiteral(" (%1)").arg(formatPercent(
                                quality.spatialRestackExcludedFrameRates[static_cast<size_t>(i)] * 100.0));
                        }
                        flaggedFrames << label;
                    }

                    md += QStringLiteral("- **Flagged Frames**: %1\n").arg(flaggedFrames.join(QStringLiteral(", ")));
                }
            }
        }

        const QString groupDirName = ImageIntegration::sanitizeFilename(group.groupKey);

        // Resolve quality maps early so the weight sum map can be promoted.
        const bool qualityMapsRequested =
            def && ImageIntegration::sanitizeStackingSettings(def->stacking).createQualityMaps;
        std::vector<QualityMapResult> qualityMaps =
            (!def || qualityMapsRequested) ? group.qualityMaps : std::vector<QualityMapResult>{};
        if (qualityMaps.empty() && qualityMapsRequested)
        {
            const QFileInfo outInfo(resolvePathForRead(group.outputPath));
            if (outInfo.exists())
            {
                const auto specs = ImageIntegration::qualityMapsForOutput(outInfo, true);
                const ArtifactTracker tracker(outInfo.absolutePath());
                for (const auto& spec : specs)
                {
                    const QString mapPath = outInfo.dir().filePath(spec.filename);
                    const QFileInfo mapInfo(mapPath);
                    if (!mapInfo.exists())
                    {
                        continue;
                    }
                    if (!ImageIntegration::isStackingAuxiliaryOutputCurrent(tracker, outInfo.absoluteFilePath(),
                                                                            mapPath))
                    {
                        continue;
                    }
                    qualityMaps.push_back(QualityMapResult{spec.title, mapPath, {}});
                }
            }
        }

        const QString reportMasterPreview = QStringLiteral("report_previews/%1/master.jpg").arg(groupDirName);
        const QString reportMasterPreviewAbs = resolvePathForRead(reportMasterPreview);
        const bool hasReportMasterPreview =
            !reportMasterPreviewAbs.isEmpty() && QFileInfo(reportMasterPreviewAbs).exists();

        if (hasReportMasterPreview || !group.previewPath.trimmed().isEmpty())
        {
            const QString previewToEmbed = hasReportMasterPreview ? reportMasterPreview : group.previewPath;
            md += buildMediaCardHtml(QStringLiteral("Master Preview"), previewToEmbed, stats);
            md += QStringLiteral("\n\n");
        }

        // Promote weight sum map to primary preview when per-pixel weighting was used.
        int promotedWeightSumMapIndex = -1;
        if (group.qualityReport.hasLocalWeightingStats)
        {
            bool foundImage = false;
            for (int qi = 0; qi < static_cast<int>(qualityMaps.size()); ++qi)
            {
                if (!qualityMaps[qi].title.contains(QStringLiteral("Weight Sum"), Qt::CaseInsensitive))
                {
                    continue;
                }

                const int mapIndex = qi + 1; // 1-based preview index
                const QString reportMapPreviewJpg =
                    QStringLiteral("report_previews/%1/quality_map_%2.jpg").arg(groupDirName).arg(mapIndex);
                const QString reportMapPreviewJpgAbs = resolvePathForRead(reportMapPreviewJpg);
                const bool hasPreview = !reportMapPreviewJpgAbs.isEmpty() && QFileInfo(reportMapPreviewJpgAbs).exists();

                const QString previewPath = hasPreview ? reportMapPreviewJpg : qualityMaps[qi].previewPath;
                if (!previewPath.trimmed().isEmpty())
                {
                    const QFileInfo mapInfo(resolvePathForRead(qualityMaps[qi].path));
                    const QString mapFile = mapInfo.fileName().isEmpty() ? qualityMaps[qi].path : mapInfo.fileName();
                    const QString mapSize = mapInfo.exists() ? formatFileSize(mapInfo.size()) : QStringLiteral("n/a");
                    const QString subtitle = QStringLiteral("%1 \u2022 %2").arg(mapFile, mapSize);

                    md += QStringLiteral("\n**Per-Pixel Weight Distribution**\n\n");
                    md += buildSecondaryImageCardHtml(QStringLiteral("Weight Sum Map"), previewPath, subtitle);
                    md += QStringLiteral("\n\n_Accumulated weight per pixel, normalized to 0\u20131. ");
                    md += QStringLiteral("Bright areas received more total weight during stacking; dark areas less. ");
                    md += QStringLiteral("Fine texture reflects the per-pixel weighting grid interpolation. ");
                    md += QStringLiteral(
                        "A mostly uniform map means weights are evenly distributed across the field._\n\n");
                    promotedWeightSumMapIndex = qi;
                    foundImage = true;
                }
                break;
            }

            // Fallback: show stats-only card when no weight sum map image is available.
            if (!foundImage)
            {
                const auto& q = group.qualityReport;
                md += QStringLiteral("\n**Per-Pixel Weight Distribution**\n\n");
                md += QStringLiteral("<div class=\"an-secondary-card\">");
                md += QStringLiteral("<strong>Spatially Varying Weights</strong><br/>");
                md += QStringLiteral("<span>Window %1 | Grid %2 px | SNR exp %3</span><br/>")
                          .arg(q.localWeightWindowSize)
                          .arg(q.localWeightGridSpacing)
                          .arg(formatCompactDouble(q.localWeightSnrExponent));
                md += QStringLiteral("<span>%1 grids | range %2\u2013%3 | mean %4 | stddev %5</span><br/>")
                          .arg(q.localWeightGridCount)
                          .arg(formatCompactDouble(q.localWeightMin))
                          .arg(formatCompactDouble(q.localWeightMax))
                          .arg(formatCompactDouble(q.localWeightMean))
                          .arg(formatCompactDouble(q.localWeightStddev));
                md += QStringLiteral("<span class=\"an-hint\">Enable Quality Maps in stacking settings for a full "
                                     "weight sum map preview.</span>");
                md += QStringLiteral("</div>\n\n");
            }
        }

        std::vector<RejectionMapResult> rejectionMaps = group.rejectionMaps;
        if (rejectionMaps.empty() && def && def->stacking.createRejectionMaps)
        {
            const QFileInfo outInfo(resolvePathForRead(group.outputPath));
            if (outInfo.exists())
            {
                const auto specs = ImageIntegration::rejectionMapsForOutput(outInfo, def->stacking.mergeRejectionMaps);
                const ArtifactTracker tracker(outInfo.absolutePath());
                for (const auto& spec : specs)
                {
                    const QString mapPath = outInfo.dir().filePath(spec.filename);
                    const QFileInfo mapInfo(mapPath);
                    if (!mapInfo.exists())
                    {
                        continue;
                    }
                    if (!ImageIntegration::isStackingAuxiliaryOutputCurrent(tracker, outInfo.absoluteFilePath(),
                                                                            mapPath))
                    {
                        continue;
                    }
                    rejectionMaps.push_back(RejectionMapResult{spec.title, mapPath, {}});
                }
            }
        }

        if (!rejectionMaps.empty())
        {
            md += QStringLiteral("- **Rejection Maps**: %1\n").arg(static_cast<int>(rejectionMaps.size()));
            int expectedMapCount = static_cast<int>(rejectionMaps.size());
            if (def && def->stacking.createRejectionMaps)
            {
                expectedMapCount = def->stacking.mergeRejectionMaps ? 1 : 2;
            }
            if (expectedMapCount > 0)
            {
                const double coveragePct = std::clamp(static_cast<double>(rejectionMaps.size()) * 100.0 /
                                                          static_cast<double>(expectedMapCount),
                                                      0.0, 100.0);
                md += QStringLiteral("- **Rejection Map Coverage**: %1 / %2 (%3)\n")
                          .arg(static_cast<int>(rejectionMaps.size()))
                          .arg(expectedMapCount)
                          .arg(formatPercent(coveragePct));
            }
            md += QStringLiteral("\n<details>\n<summary>Rejection maps (secondary)</summary>\n\n");
            md += QStringLiteral("<div class=\"an-secondary-gallery\">\n");

            int mapIndex = 1;
            for (const auto& map : rejectionMaps)
            {
                const QString mapTitle = map.title.trimmed().isEmpty() ? QStringLiteral("Rejection Map") : map.title;

                const QFileInfo mapInfo(resolvePathForRead(map.path));
                const QString mapFile = mapInfo.fileName().isEmpty() ? map.path : mapInfo.fileName();
                const QString mapSize = mapInfo.exists() ? formatFileSize(mapInfo.size()) : QStringLiteral("n/a");
                QString subtitle = QStringLiteral("%1 \u2022 %2").arg(mapFile, mapSize);
                if (group.qualityReport.hasSpatialRejectionClusters)
                {
                    const auto mapKind = rejectionMapKindForTitle(mapTitle);
                    const auto clusterCount = static_cast<int>(std::count_if(
                        group.qualityReport.spatialClusters.begin(), group.qualityReport.spatialClusters.end(),
                        [mapKind](const RejectionClusterRecord& cluster) { return cluster.mapKind == mapKind; }));
                    if (clusterCount > 0)
                    {
                        subtitle += QStringLiteral(" \u2022 %1 cluster(s) highlighted").arg(clusterCount);
                    }
                }

                const QString reportMapPreviewJpg =
                    QStringLiteral("report_previews/%1/rejection_map_%2.jpg").arg(groupDirName).arg(mapIndex);
                const QString reportMapPreviewJpgAbs = resolvePathForRead(reportMapPreviewJpg);
                const bool hasReportMapPreviewJpg =
                    !reportMapPreviewJpgAbs.isEmpty() && QFileInfo(reportMapPreviewJpgAbs).exists();

                const QString reportMapPreviewPng =
                    QStringLiteral("report_previews/%1/rejection_map_%2.png").arg(groupDirName).arg(mapIndex);
                const QString reportMapPreviewPngAbs = resolvePathForRead(reportMapPreviewPng);
                const bool hasReportMapPreviewPng =
                    !reportMapPreviewPngAbs.isEmpty() && QFileInfo(reportMapPreviewPngAbs).exists();

                const QString previewPathToEmbed =
                    hasReportMapPreviewJpg ? reportMapPreviewJpg
                                           : (hasReportMapPreviewPng ? reportMapPreviewPng : map.previewPath);
                if (!previewPathToEmbed.trimmed().isEmpty())
                {
                    md += buildSecondaryImageCardHtml(mapTitle, previewPathToEmbed, subtitle);
                }
                ++mapIndex;
            }

            md += QStringLiteral("\n</div>\n\n</details>\n\n");
        }

        // qualityMaps was already resolved above for weight sum map promotion.

        if (!qualityMaps.empty())
        {
            const int displayCount = (promotedWeightSumMapIndex >= 0) ? static_cast<int>(qualityMaps.size()) - 1
                                                                      : static_cast<int>(qualityMaps.size());

            if (displayCount > 0)
            {
                md += QStringLiteral("- **Quality Maps**: %1\n").arg(static_cast<int>(qualityMaps.size()));
                md += QStringLiteral("\n<details>\n<summary>Quality maps</summary>\n\n");
                md += QStringLiteral(
                    "_All quality maps are normalized to 0\u20131. Bright = high relative value, dark = low._\n\n");

                // Per-map reference
                md += QStringLiteral("| Map | What it shows | How to read it |\n");
                md += QStringLiteral("|---|---|---|\n");
                md += QStringLiteral(
                    "| **Coverage** | Number of frames that contributed to each pixel, relative to the maximum | "
                    "Uniform \u2192 every pixel received the same number of frames. "
                    "Dark edges \u2192 dithering or registration reduced frame overlap there. "
                    "Internal dark spots \u2192 rejection removed frames at those positions |\n");
                md += QStringLiteral(
                    "| **Variance** | Sample variance of the input pixel values at each position | "
                    "Bright regions \u2192 greater spread across frames (noise, nebulosity, or residual gradients). "
                    "Low/uniform \u2192 frames are consistent and well-calibrated |\n");
                md += QStringLiteral(
                    "| **SNR Gain** | Stacking noise-reduction factor (\u221Acoverage), relative to the maximum | "
                    "Tracks the same spatial pattern as Coverage but with compressed dynamic range. "
                    "Example: 9 frames \u2192 3\u00D7 SNR improvement. "
                    "Uniform \u2192 even noise reduction across the field |\n");
                if (promotedWeightSumMapIndex >= 0 || group.qualityReport.hasLocalWeightingStats)
                {
                    md += QStringLiteral(
                        "| **Weight Sum** | Total accumulated weight per pixel from per-image or per-pixel weighting | "
                        "Bright \u2192 more weight (higher quality data). "
                        "Spatial texture reflects the per-pixel weighting grid. "
                        "Uniform \u2192 weights are evenly distributed across the field |\n");
                }
                md += QStringLiteral("\n");
                md += QStringLiteral("<div class=\"an-secondary-gallery\">\n");
            }

            int mapIndex = 1;
            for (int qi = 0; qi < static_cast<int>(qualityMaps.size()); ++qi)
            {
                if (qi == promotedWeightSumMapIndex)
                {
                    ++mapIndex;
                    continue;
                }

                const auto& map = qualityMaps[qi];
                const QString mapTitle = map.title.trimmed().isEmpty() ? QStringLiteral("Quality Map") : map.title;

                const QFileInfo mapInfo(resolvePathForRead(map.path));
                const QString mapFile = mapInfo.fileName().isEmpty() ? map.path : mapInfo.fileName();
                const QString mapSize = mapInfo.exists() ? formatFileSize(mapInfo.size()) : QStringLiteral("n/a");
                const QString subtitle = QStringLiteral("%1 \u2022 %2").arg(mapFile, mapSize);

                const QString reportMapPreviewJpg =
                    QStringLiteral("report_previews/%1/quality_map_%2.jpg").arg(groupDirName).arg(mapIndex);
                const QString reportMapPreviewJpgAbs = resolvePathForRead(reportMapPreviewJpg);
                const bool hasReportMapPreviewJpg =
                    !reportMapPreviewJpgAbs.isEmpty() && QFileInfo(reportMapPreviewJpgAbs).exists();

                const QString previewPathToEmbed = hasReportMapPreviewJpg ? reportMapPreviewJpg : map.previewPath;
                if (!previewPathToEmbed.trimmed().isEmpty())
                {
                    md += buildSecondaryImageCardHtml(mapTitle, previewPathToEmbed, subtitle);
                }
                else
                {
                    md += QStringLiteral(
                              "<div class=\"an-secondary-card\"><strong>%1</strong><br/><span>%2</span></div>\n")
                              .arg(mapTitle.toHtmlEscaped(), subtitle.toHtmlEscaped());
                }
                ++mapIndex;
            }

            if (displayCount > 0)
            {
                md += QStringLiteral("\n</div>\n\n</details>\n\n");
            }
        }

        md += QStringLiteral("\n");
    }

    const bool has_failed_finalization_attempt = std::any_of(
        m_result.integrationGroupResults.begin(), m_result.integrationGroupResults.end(),
        [](const IntegrationGroupResult& group) { return !group.success && group.finalizationTiming.captured; });
    if (has_failed_finalization_attempt)
    {
        md += QStringLiteral("### Incomplete Stack Group Finalization Attempts\n\n");
        md += QStringLiteral(
            "_Only work reached by each failed or cancelled attempt is timed; later phases remain explicitly "
            "skipped at 0ms._\n\n");
        for (const auto& group : m_result.integrationGroupResults)
        {
            if (group.success || !group.finalizationTiming.captured)
            {
                continue;
            }

            md += QStringLiteral("#### %1\n").arg(friendlyGroupKey(group.groupKey));
            md += QStringLiteral("- Duration (parent): %1\n\n").arg(formatDuration(group.durationMs));
            md += QStringLiteral("| Finalization Phase | Status | Duration |\n"
                                 "|---|---|---:|\n");
            for (const auto& phase : group.finalizationTiming.phases)
            {
                md += QStringLiteral("| %1 | %2 | %3 |\n")
                          .arg(phase.stageName, phase.status, formatDuration(phase.durationMs));
            }
            md += QStringLiteral("\n");
        }
    }
}

void WorkflowReportBuilder::appendCalibrationSummary(QString& md) const
{
    bool hasCalibration = (m_manifest.biasStage.has_value() && !m_manifest.biasStage->groups.empty()) ||
                          (m_manifest.darkStage.has_value() && !m_manifest.darkStage->groups.empty()) ||
                          (m_manifest.flatStage.has_value() && !m_manifest.flatStage->groups.empty());

    if (!hasCalibration && m_manifest.preCalibratedLightGroups.empty())
        return;

    appendSectionAnchor(md, QStringLiteral("calibration-summary"));
    md += QStringLiteral("## \U0001F527 Calibration Summary\n");
    md += QStringLiteral("_Interpretation: calibration master provenance. Groups/Input Frames summarize bias/dark/flat "
                         "sources used by this workflow manifest._\n");

    // A pre-calibrated group has no calibration entry, as a group no master matched has none; naming it tells the two
    // apart.
    if (!m_manifest.preCalibratedLightGroups.empty())
    {
        QStringList groups;
        for (const auto& group : m_manifest.preCalibratedLightGroups)
        {
            groups << QStringLiteral("%1 (%2 light(s))")
                          .arg(friendlyGroupKey(group.groupKey.toString()))
                          .arg(static_cast<int>(group.lightFiles.size()));
        }
        md += QStringLiteral("- **Pre-calibrated light groups** (marked by the user; no master is applied): %1\n")
                  .arg(groups.join(QStringLiteral("; ")).toHtmlEscaped());
    }
    if (!hasCalibration)
    {
        md += QStringLiteral("\n");
        return;
    }

    const bool ranCalibration = didStageRun(QStringLiteral("Bias")) || didStageRun(QStringLiteral("Dark")) ||
                                didStageRun(QStringLiteral("Flat"));

    if (!ranCalibration)
    {
        md += QStringLiteral("_Calibration stacking was not executed in this run. Existing masters were reused._\n\n");
    }

    const int biasCount = m_manifest.biasStage.has_value() ? static_cast<int>(m_manifest.biasStage->groups.size()) : 0;
    const int darkCount = m_manifest.darkStage.has_value() ? static_cast<int>(m_manifest.darkStage->groups.size()) : 0;
    const int flatCount = m_manifest.flatStage.has_value() ? static_cast<int>(m_manifest.flatStage->groups.size()) : 0;
    md += QStringLiteral("- **Bias Groups**: %1\n").arg(biasCount);
    md += QStringLiteral("- **Dark Groups**: %1\n").arg(darkCount);
    md += QStringLiteral("- **Flat Groups**: %1\n\n").arg(flatCount);

    md += QStringLiteral("<details>\n<summary>Calibration groups used</summary>\n\n");
    md += QStringLiteral("| Type | Group | Input Frames | Master Path | Stacking Settings |\n");
    md += QStringLiteral("|---|---|---|---|---|\n");

    auto appendStage = [&](const std::optional<WorkflowManifest::CalibrationStage>& stage, const QString& type) {
        if (!stage.has_value())
            return;
        for (const auto& g : stage->groups)
        {
            md += QStringLiteral("| %1 | %2 | %3 | %4 | %5 |\n")
                      .arg(type, friendlyGroupKey(g.groupKey.toString()), QString::number(g.inputFiles.size()),
                           g.outputFilename.toHtmlEscaped(),
                           stage_utils::describeStackingSettings(g.stacking).toHtmlEscaped());
        }
    };

    appendStage(m_manifest.biasStage, "Bias");
    appendStage(m_manifest.darkStage, "Dark");
    appendStage(m_manifest.flatStage, "Flat");
    md += QStringLiteral("\n</details>\n\n");
}

void WorkflowReportBuilder::appendRegistrationSummary(QString& md) const
{
    if (m_manifest.registrationGroups.empty())
        return;

    appendSectionAnchor(md, QStringLiteral("registration-summary"));
    md += QStringLiteral("## \U0001F4CD Registration Summary\n");
    md += QStringLiteral("_Interpretation: alignment provenance per registration group. Ref Frame is the reference "
                         "light used for transforms._\n");
    const bool ranRegistration =
        didStageRun(QStringLiteral("LightsRegistration")) || didStageRun(QStringLiteral("LightsAlignment"));
    if (!ranRegistration)
    {
        md += QStringLiteral(
            "_Registration was not executed in this run. Existing alignment references were reused._\n\n");
    }

    md += QStringLiteral("<details>\n<summary>Registration groups used (%1)</summary>\n\n")
              .arg(static_cast<int>(m_manifest.registrationGroups.size()));
    md += QStringLiteral("| Group | Lights | Ref Frame | Alignment | Transform |\n");
    md += QStringLiteral("|---|---|---|---|---|\n");

    const QString metricsDbPath = registrationMetricsDatabasePath(m_manifest, m_reportBaseDir);
    for (const auto& g : m_manifest.registrationGroups)
    {
        QString ref = QStringLiteral("-");
        if (g.referenceFrame.has_value())
        {
            ref = QFileInfo(g.referenceFrame->canonicalPath()).fileName();
        }

        md += QStringLiteral("| %1 | %2 | %3 | %4 | %5 |\n")
                  .arg(friendlyGroupKey(g.groupKey.toString()))
                  .arg(g.lightFiles.size())
                  .arg(ref)
                  .arg(g.settings.alignmentMode)
                  .arg(registrationTransformDisplayName(g.settings,
                                                        resolveAutoTransformDetail(g, metricsDbPath, m_counters).detail)
                           .toHtmlEscaped());
    }
    md += QStringLiteral("\n</details>\n\n");

    appendCometTrackingDiagnostics(md, metricsDbPath);

    // SIP distortion correction summary
    const bool has_sip = std::any_of(m_result.alignmentGroupResults.begin(), m_result.alignmentGroupResults.end(),
                                     [](const AlignmentGroupResult& ar) { return ar.sipCorrectedCount > 0; });
    if (has_sip)
    {
        md += QStringLiteral("<div class=\"an-callout-info\">\n\n");
        md += QStringLiteral("#### \U0001F4D0 SIP Distortion Correction\n\n");
        md += QStringLiteral("SIP (_Simple Imaging Polynomial_) is a FITS standard extension that models optical ");
        md += QStringLiteral("distortion of the telescope and camera system as polynomial coefficients ");
        md += QStringLiteral("(A/B for forward, AP/BP for inverse). ");
        md += QStringLiteral("When present in the frame FITS headers, AfterNight uses these coefficients to correct ");
        md +=
            QStringLiteral("barrel, pincushion, and higher-order distortions during star matching and image warping, ");
        md +=
            QStringLiteral("enabling sub-pixel alignment accuracy that simple affine or projective transforms alone ");
        md += QStringLiteral("cannot achieve.\n\n");
        md += QStringLiteral("| Group | SIP-corrected frames | Total aligned |\n");
        md += QStringLiteral("|---|---|---|\n");
        for (const auto& ar : m_result.alignmentGroupResults)
        {
            if (ar.sipCorrectedCount > 0)
            {
                md += QStringLiteral("| %1 | %2 | %3 |\n")
                          .arg(friendlyGroupKey(ar.groupKey))
                          .arg(ar.sipCorrectedCount)
                          .arg(ar.totalAligned);
            }
        }
        md += QStringLiteral("\n</div>\n\n");
    }

    // Chromatic aberration correction summary
    const bool has_ca = std::any_of(m_result.alignmentGroupResults.begin(), m_result.alignmentGroupResults.end(),
                                    [](const AlignmentGroupResult& ar) { return ar.caCorrectedCount > 0; });
    if (has_ca)
    {
        md += QStringLiteral("<div class=\"an-callout-info\">\n\n");
        md += QStringLiteral("#### \U0001F308 Chromatic Aberration Correction\n\n");
        md += QStringLiteral("Chromatic aberration (CA) occurs when a lens focuses different wavelengths at slightly ");
        md += QStringLiteral("different positions, causing RGB channels to be misregistered. ");
        md += QStringLiteral("AfterNight corrects lateral CA by detecting stars independently in each color channel, ");
        md += QStringLiteral("matching red and blue channel stars against green, and applying per-channel alignment ");
        md += QStringLiteral("transforms before registration.\n\n");
        md += QStringLiteral("| Group | CA-corrected frames | Passthrough frames |\n");
        md += QStringLiteral("|---|---|---|\n");
        for (const auto& ar : m_result.alignmentGroupResults)
        {
            if (ar.caCorrectedCount > 0)
            {
                md += QStringLiteral("| %1 | %2 | %3 |\n")
                          .arg(friendlyGroupKey(ar.groupKey))
                          .arg(ar.caCorrectedCount)
                          .arg(ar.caPassthroughCount);
            }
        }
        md += QStringLiteral("\n</div>\n\n");
    }

    appendSpccHandoffDiagnostics(md);
}

void WorkflowReportBuilder::appendCometTrackingDiagnostics(QString& md, const QString& metricsDbPath) const
{
    const QList<CometReportRun> reportRuns = loadCometReportRuns(metricsDbPath, m_manifest, m_counters);
    if (reportRuns.isEmpty())
    {
        return;
    }

    md += QStringLiteral("<div class=\"an-callout-info\">\n\n");
    md += QStringLiteral("#### Comet Tracking\n\n");
    md += QStringLiteral("_Persisted comet sidecar diagnostics from `registration_metrics.db`. These records describe "
                         "candidate and registration-ready comet transforms without replacing ordinary registration "
                         "metrics._\n\n");
    md += QStringLiteral("| Group | Run status | Mode | Fit | Readiness | Inputs | Points | RMS px | Max residual px | "
                         "Transforms | Output plane | Warnings |\n");
    md += QStringLiteral("|---|---|---|---|---|---|---|---:|---:|---|---|---|\n");

    for (const CometReportRun& reportRun : reportRuns)
    {
        const QString fit = reportRun.summary.has_value() ? reportRun.summary->fitModel : QStringLiteral("-");
        const QString rms =
            reportRun.summary.has_value() ? formatCompactDouble(reportRun.summary->rmsPixels) : QStringLiteral("-");
        const QString maxResidual = reportRun.summary.has_value()
                                        ? formatCompactDouble(reportRun.summary->maxResidualPixels)
                                        : QStringLiteral("-");
        const QString warnings =
            reportRun.summary.has_value() ? reportRun.summary->warningFlags.trimmed() : QStringLiteral("-");

        md += QStringLiteral("| %1 | %2 | %3 | %4 | %5 | %6 | %7 | %8 | %9 | %10 | %11 | %12 |\n")
                  .arg(markdownTableCell(friendlyGroupKey(reportRun.run.groupKey)))
                  .arg(markdownTableCell(cometDisplayToken(reportRun.run.status)))
                  .arg(markdownTableCell(cometDisplayToken(reportRun.run.mode)))
                  .arg(markdownTableCell(fit))
                  .arg(markdownTableCell(cometReadinessText(reportRun.cometAlignedProduct)))
                  .arg(markdownTableCell(cometInputSummaryText(reportRun.summary)))
                  .arg(markdownTableCell(cometPointSummaryText(reportRun.points)))
                  .arg(markdownTableCell(rms))
                  .arg(markdownTableCell(maxResidual))
                  .arg(markdownTableCell(cometTransformSummaryText(reportRun.cometAlignedProduct)))
                  .arg(markdownTableCell(cometOutputPlaneText(reportRun.cometAlignedProduct)))
                  .arg(markdownTableCell(warnings));
    }

    md += QStringLiteral("\n</div>\n\n");
}

void WorkflowReportBuilder::appendSpccHandoffDiagnostics(QString& md) const
{
    const QString outputDir = m_manifest.outputDirectory.trimmed();
    const QString metricsDbPath =
        outputDir.isEmpty() ? QString() : QDir(outputDir).filePath(QStringLiteral("registration_metrics.db"));
    if (metricsDbPath.isEmpty() || !QFileInfo::exists(metricsDbPath))
    {
        return;
    }

    const QString connectionName =
        QStringLiteral("WorkflowReportSpccHandoff_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(metricsDbPath);
    if (!db.open())
    {
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        return;
    }

    const QList<SpccHandoffGroupSummary> summaries = loadSpccHandoffSummaries(db, m_manifest, m_counters);
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);

    if (summaries.isEmpty())
    {
        return;
    }

    md += QStringLiteral("<div class=\"an-callout-info\">\n\n");
    md += QStringLiteral("#### SPCC Source-ID Handoff Diagnostics\n\n");
    md += QStringLiteral("_Persisted Gaia `source_id` references exported from `registration_metrics.db` for later "
                         "Spectrophotometric Color Calibration reuse._\n\n");
    md += QStringLiteral(
        "| Registration Group | Settings | Frames | Unique Source IDs | Satisfied References | Other Statuses | "
        "SPCC Manifest | Astrometric Manifest | Median Residual | Median Separation |\n");
    md += QStringLiteral("|---|---:|---:|---:|---:|---|---|---|---:|---:|\n");

    for (const auto& summary : summaries)
    {
        md += QStringLiteral("| %1 | v%2 | %3 | %4 | %5/%6 | %7 | %8 | %9 | %10 | %11 |\n")
                  .arg(summary.groupName.toHtmlEscaped())
                  .arg(summary.settingsVersion)
                  .arg(summary.frameCount)
                  .arg(summary.sourceCount)
                  .arg(summary.satisfiedCount)
                  .arg(summary.referenceCount)
                  .arg(statusBreakdownText(summary).toHtmlEscaped())
                  .arg(compactManifestId(summary.spccManifestId).toHtmlEscaped())
                  .arg(compactManifestId(summary.astrometricManifestId).toHtmlEscaped())
                  .arg(formatOptionalCompactDouble(medianOf(summary.residualsPx), QStringLiteral("px")))
                  .arg(formatOptionalCompactDouble(medianOf(summary.matchSeparationsArcsec), QStringLiteral("arcsec")));
    }

    md += QStringLiteral("\n</div>\n\n");
}

void WorkflowReportBuilder::appendFullProcessingParameters(QString& md) const
{
    if (m_manifest.integrationGroups.empty())
    {
        return;
    }

    QHash<QString, const WorkflowManifest::RegistrationGroup*> registrationByKey;
    registrationByKey.reserve(static_cast<int>(m_manifest.registrationGroups.size()));
    for (const auto& registrationGroup : m_manifest.registrationGroups)
    {
        registrationByKey.insert(registrationGroup.groupKey.toString(), &registrationGroup);
    }
    const QString metricsDbPath = registrationMetricsDatabasePath(m_manifest, m_reportBaseDir);

    appendSectionAnchor(md, QStringLiteral("full-processing-parameters"));
    md += QStringLiteral("## \U0001F9F0 Full Processing Parameters\n");
    md += QStringLiteral("_Interpretation: complete stacking/normalization/registration settings captured in the "
                         "workflow manifest for reproducibility._\n\n");

    const int resolvedParallelCores = ImageIntegration::core::clampParallelCoreLimit(m_manifest.maxParallelCores);
    const QString parallelCoreSummary = m_manifest.maxParallelCores > 0
                                            ? QStringLiteral("%1 of %2")
                                                  .arg(resolvedParallelCores)
                                                  .arg(ImageIntegration::core::detectedParallelCoreCount())
                                            : QStringLiteral("All available (%1)").arg(resolvedParallelCores);
    md += QStringLiteral("### Execution\n\n");
    md += QStringLiteral("| Parameter | Value |\n");
    md += QStringLiteral("|---|---|\n");
    md += QStringLiteral("| Parallel CPU Threads | %1 |\n\n").arg(parallelCoreSummary.toHtmlEscaped());

    md += QStringLiteral("### Registration (Shared)\n\n");
    md += QStringLiteral("_Registration settings are typically shared across integration groups; detailed settings are "
                         "listed once here._\n\n");
    if (m_manifest.registrationGroups.empty())
    {
        md += QStringLiteral("_No registration groups were recorded in the workflow manifest._\n\n");
    }
    else
    {
        for (const auto& registrationGroup : m_manifest.registrationGroups)
        {
            const auto& reg = registrationGroup.settings;
            const QString regName = friendlyGroupKey(registrationGroup.groupKey.toString());

            md +=
                QStringLiteral("<details>\n<summary>Registration Group: %1</summary>\n\n").arg(regName.toHtmlEscaped());
            md += QStringLiteral("| Parameter | Value |\n");
            md += QStringLiteral("|---|---|\n");
            md += QStringLiteral("| Registration Group | %1 |\n").arg(regName.toHtmlEscaped());
            md += QStringLiteral("| Settings Version | %1 |\n").arg(reg.version);
            md += QStringLiteral("| Auto-reference During Registration | %1 |\n")
                      .arg(boolText(reg.autoReferenceDuringRegistration));
            md += QStringLiteral("| Reference Image | %1 |\n")
                      .arg(reg.referenceImage.trimmed().isEmpty()
                               ? QStringLiteral("Automatic")
                               : QFileInfo(reg.referenceImage).fileName().toHtmlEscaped());
            md += QStringLiteral("| Registration Method | %1 |\n")
                      .arg(ImageIntegration::RegistrationSettingsUtils::registrationMethodDisplayName(
                               reg.registrationMethod)
                               .toHtmlEscaped());
            md += QStringLiteral("| Chosen Reference Frame | %1 |\n")
                      .arg(registrationGroup.referenceFrame.has_value()
                               ? frameLabel(registrationGroup.referenceFrame.value()).toHtmlEscaped()
                               : QStringLiteral("Automatic"));
            if (ImageIntegration::RegistrationSettingsUtils::usesStarDetection(reg.registrationMethod))
            {
                md += QStringLiteral("| Min Star Pairs | %1 |\n").arg(reg.minStarPairs);
                md += QStringLiteral("| Max Stars per Frame | %1 |\n").arg(reg.maxStarPairs);
            }
            md += QStringLiteral("| Transform Model | %1 |\n")
                      .arg(registrationTransformDisplayName(
                               reg, resolveAutoTransformDetail(registrationGroup, metricsDbPath, m_counters).detail)
                               .toHtmlEscaped());
            if (ImageIntegration::RegistrationSettingsUtils::usesCometTracking(reg.registrationMethod) &&
                reg.cometTracking.has_value())
            {
                int enabled_observations = 0;
                for (const auto& observation : reg.cometTracking->observations)
                {
                    if (observation.enabled)
                    {
                        ++enabled_observations;
                    }
                }
                md += QStringLiteral("| Comet Settings Schema | %1 |\n").arg(reg.cometTracking->schemaVersion);
                md += QStringLiteral("| Comet Observations | %1 total, %2 enabled |\n")
                          .arg(reg.cometTracking->observations.size())
                          .arg(enabled_observations);
            }
            if (ImageIntegration::RegistrationSettingsUtils::usesTransformContract(reg.registrationMethod))
            {
                md += QStringLiteral("| Base Transform | %1 |\n").arg(reg.transformation.toHtmlEscaped());
                md += QStringLiteral("| Transform Mode | %1 |\n").arg(reg.transformMode.toHtmlEscaped());
                md += QStringLiteral("| Transform Order | %1 |\n").arg(reg.transformOrder);
                if (ImageIntegration::RegistrationSettingsUtils::usesManualControlPoints(reg.registrationMethod))
                {
                    int manual_point_count = 0;
                    for (auto it = reg.manualControlPointsByFrame.cbegin(); it != reg.manualControlPointsByFrame.cend();
                         ++it)
                    {
                        manual_point_count += it.value().size();
                    }
                    md += QStringLiteral("| Manual Control-Point Frames | %1 |\n")
                              .arg(reg.manualControlPointsByFrame.size());
                    md += QStringLiteral("| Manual Control-Point Pairs | %1 |\n").arg(manual_point_count);
                }
            }
            md += QStringLiteral("| Alignment Mode | %1 |\n").arg(reg.alignmentMode.toHtmlEscaped());
            md += QStringLiteral("| Interpolation | %1 |\n")
                      .arg(interpolationDisplayText(reg.interpolation).toHtmlEscaped());
            if (ImageIntegration::RegistrationSettingsUtils::usesStarDetection(reg.registrationMethod))
            {
                md += QStringLiteral("| Detection Radius | %1 |\n").arg(reg.radius);
                md += QStringLiteral("| Detection Threshold | %1 |\n").arg(formatStatValue(reg.threshold));
                md += QStringLiteral("| Amplitude Range Enabled | %1 |\n").arg(boolText(reg.amplitudeRangeEnabled));
                md += QStringLiteral("| Threshold Minimum | %1 |\n").arg(formatStatValue(reg.thresholdMinimum));
                md += QStringLiteral("| Threshold Maximum | %1 |\n").arg(formatStatValue(reg.thresholdMaximum));
                md += QStringLiteral("| Roundness Threshold | %1 |\n").arg(formatStatValue(reg.roundnessThreshold));
                md += QStringLiteral("| Roundness Range Enabled | %1 |\n").arg(boolText(reg.roundnessRangeEnabled));
                md += QStringLiteral("| Roundness Threshold Maximum | %1 |\n")
                          .arg(formatStatValue(reg.roundnessThresholdMaximum));
                md += QStringLiteral("| Convergence | %1 |\n").arg(reg.convergence);
                md += QStringLiteral("| Relax PSF Checks | %1 |\n").arg(boolText(reg.relaxPsfChecks));
                md += QStringLiteral("| Profile Type | %1 |\n").arg(reg.profileType.toHtmlEscaped());
                md += QStringLiteral("| Minimum Beta | %1 |\n").arg(formatStatValue(reg.minimumBeta));
            }
            md += QStringLiteral("| CFA Pattern | %1 |\n").arg(cfaPatternDisplayText(reg.cfaPattern).toHtmlEscaped());
            md += QStringLiteral("| Debayer Method | %1 |\n")
                      .arg(debayerMethodDisplayText(reg.debayerMethod).toHtmlEscaped());
            md += QStringLiteral("\n</details>\n\n");
        }
    }

    for (const auto& integrationGroup : m_manifest.integrationGroups)
    {
        const QString groupName = friendlyGroupKey(integrationGroup.groupKey.toString());
        const ExposureBreakdown exposureBreakdown = buildExposureBreakdown(integrationGroup);
        const bool normalizationEnabled = integrationGroup.stacking.normalizationMode != NormalizationMode::None;
        const bool localNormalizationEnabled =
            normalizationEnabled && integrationGroup.stacking.normalizationMode == NormalizationMode::Local &&
            integrationGroup.stacking.localNormalization.enabled;
        const bool manualNormalizationReference =
            integrationGroup.stacking.normalizationReferenceMode == NormalizationReferenceMode::Manual &&
            !integrationGroup.stacking.manualNormalizationReference.trimmed().isEmpty();
        const bool explicitLocalReferenceStrategy = ImageIntegration::isExplicitLocalNormReferenceStrategy(
            integrationGroup.stacking.localNormalization.referenceStrategy);
        const bool referencesSelectedByUser = manualNormalizationReference || explicitLocalReferenceStrategy;
        const QStringList normalizationRefs = [&]() {
            QStringList refs;
            refs.reserve(static_cast<int>(integrationGroup.normalizationReferenceFrames.size()));
            for (const auto& frameId : integrationGroup.normalizationReferenceFrames)
            {
                const QString label = frameLabel(frameId);
                if (!label.trimmed().isEmpty() && label != QStringLiteral("-"))
                {
                    refs << label;
                }
            }
            refs.removeDuplicates();
            refs.sort(Qt::CaseInsensitive);
            return refs;
        }();

        md += QStringLiteral("<details>\n<summary>%1</summary>\n\n").arg(groupName.toHtmlEscaped());

        const auto regIt = registrationByKey.constFind(integrationGroup.registrationGroupKey.toString());
        const WorkflowManifest::RegistrationGroup* registrationGroup =
            regIt != registrationByKey.constEnd() ? regIt.value() : nullptr;

        md += QStringLiteral("### Stacking\n\n");
        md += QStringLiteral("| Parameter | Value |\n");
        md += QStringLiteral("|---|---|\n");
        const QString stackMethodSummary =
            integrationGroup.stacking.hdr.enabled
                ? QStringLiteral("HDR Merge (%1)").arg(integrationGroup.stacking.stackMethod)
                : integrationGroup.stacking.stackMethod;
        md += QStringLiteral("| Stack Method | %1 |\n").arg(stackMethodSummary.toHtmlEscaped());
        const QString cometStackingMode =
            ImageIntegration::normalizeCometStackingMode(integrationGroup.stacking.cometStackingMode);
        if (ImageIntegration::usesCometStackingMode(cometStackingMode))
        {
            md += QStringLiteral("| Comet Stacking Mode | %1 |\n").arg(cometStackingMode.toHtmlEscaped());
        }
        md += QStringLiteral("| HDR Exposure Merge | %1 |\n").arg(boolText(integrationGroup.stacking.hdr.enabled));
        if (integrationGroup.stacking.hdr.enabled && exposureBreakdown.knownFrames > 0)
        {
            QString exposureSummary = formatExposureList(exposureBreakdown.buckets);
            if (exposureBreakdown.unknownFrames > 0)
            {
                exposureSummary += QStringLiteral(", %1 \u00D7 ?s").arg(exposureBreakdown.unknownFrames);
            }
            md += QStringLiteral("| HDR Exposure Buckets | %1 |\n").arg(exposureSummary.toHtmlEscaped());
        }
        md += QStringLiteral("| Rejection Method | %1 |\n")
                  .arg(rejectionMethodText(integrationGroup.stacking).toHtmlEscaped());
        md += QStringLiteral("| Sigma Low | %1 |\n").arg(formatStatValue(integrationGroup.stacking.sigmaLow));
        md += QStringLiteral("| Sigma High | %1 |\n").arg(formatStatValue(integrationGroup.stacking.sigmaHigh));
        md += QStringLiteral("| Weighting | %1 |\n").arg(integrationGroup.stacking.weighting.toHtmlEscaped());
        md += QStringLiteral("| Star Count Scaling | %1 |\n")
                  .arg(integrationGroup.stacking.starCountScaling.toHtmlEscaped());
        md += QStringLiteral("| Exponential Alpha | %1 |\n")
                  .arg(formatStatValue(integrationGroup.stacking.exponentialAlpha));
        md += QStringLiteral("| Minimum Weight Floor | %1 |\n")
                  .arg(formatStatValue(integrationGroup.stacking.minWeightFloor));
        md += QStringLiteral("| Per-channel FWHM Weighting | %1 |\n")
                  .arg(boolText(integrationGroup.stacking.perChannelFwhmWeighting));
        md += QStringLiteral("| Create Rejection Maps | %1 |\n")
                  .arg(boolText(integrationGroup.stacking.createRejectionMaps));
        md += QStringLiteral("| Merge Rejection Maps | %1 |\n")
                  .arg(boolText(integrationGroup.stacking.mergeRejectionMaps));
        md += QStringLiteral("| Normalization Mode | %1 |\n")
                  .arg(normalizationModeText(integrationGroup.stacking.normalizationMode));
        if (normalizationEnabled)
        {
            // Local normalization matches each light globally in Additive With Scaling, whatever method the group
            // stores for Global.
            md += QStringLiteral("| Global Normalization Method | %1 |\n")
                      .arg(localNormalizationEnabled
                               ? QStringLiteral("%1 (set by local normalization)")
                                     .arg(globalNormalizationMethodText(GlobalNormalizationMethod::AdditiveScaling))
                               : globalNormalizationMethodText(integrationGroup.stacking.globalNormalizationMethod));
            if (integrationGroup.stacking.normalizationMode == NormalizationMode::Local ||
                ImageIntegration::normalizationUsesScaleEstimator(integrationGroup.stacking.globalNormalizationMethod))
            {
                md += QStringLiteral("| Scale Estimator | %1 |\n")
                          .arg(scaleEstimatorText(integrationGroup.stacking.scaleEstimator));
            }
        }
        md += QStringLiteral("\n");

        if (normalizationEnabled)
        {
            md += QStringLiteral("### Normalization Reference Frame(s)\n\n");
            if (!normalizationRefs.isEmpty())
            {
                md += QStringLiteral("- **Chosen reference frame(s) (%1):** %2\n")
                          .arg(normalizationRefs.size())
                          .arg(normalizationRefs.join(QStringLiteral(", ")).toHtmlEscaped());
                if (referencesSelectedByUser)
                {
                    md += QStringLiteral("- **Reference source:** User-selected in settings\n");
                }
                else
                {
                    md += QStringLiteral(
                        "- **Reference source:** Auto-detected during integration (not selected by user)\n");
                }
            }
            else
            {
                md += QStringLiteral("- **Chosen reference frame(s):** Not recorded in manifest\n");
                if (referencesSelectedByUser)
                {
                    md += QStringLiteral("- **Reference source:** User-selected mode was configured\n");
                }
                else
                {
                    md += QStringLiteral("- **Reference source:** Automatic selection mode\n");
                }
            }
            if (manualNormalizationReference)
            {
                md += QStringLiteral("- **Manual reference requested:** %1\n")
                          .arg(QFileInfo(integrationGroup.stacking.manualNormalizationReference)
                                   .fileName()
                                   .toHtmlEscaped());
            }
            if (integrationGroup.normalizationCoefficientsArtifactPath.has_value() &&
                !integrationGroup.normalizationCoefficientsArtifactPath->trimmed().isEmpty())
            {
                md += QStringLiteral("- **Normalization Coefficients Artifact:** `%1`\n")
                          .arg(integrationGroup.normalizationCoefficientsArtifactPath.value().toHtmlEscaped());
            }
            md += QStringLiteral("\n");
        }
        else
        {
            md += QStringLiteral("_Normalization disabled for this group; normalization reference and parameter "
                                 "details are omitted._\n\n");
        }

        if (localNormalizationEnabled)
        {
            md += QStringLiteral("### Local Normalization Settings\n\n");
            if (integrationGroup.drizzle.has_value() && integrationGroup.drizzle->enabled)
            {
                // A drizzled group has no normalization stage: its stack fits the surfaces on the drizzled lights.
                md += QStringLiteral("_The drizzled stack fits these surfaces against the group's reference: the "
                                     "reference strategy, the evaluation, the PSF flux scale and the hot-pixel and "
                                     "noise filters do not apply._\n\n");
            }
            const auto& localNorm = integrationGroup.stacking.localNormalization;
            md += QStringLiteral("| Parameter | Value |\n");
            md += QStringLiteral("|---|---|\n");
            md += QStringLiteral("| Enabled | %1 |\n").arg(boolText(localNorm.enabled));
            md += QStringLiteral("| Reference Strategy | %1 |\n").arg(localNorm.referenceStrategy.toHtmlEscaped());
            md += QStringLiteral("| Max Integrated Frames | %1 |\n").arg(localNorm.maxIntegratedFrames);
            md += QStringLiteral("| Grid Size | %1 |\n").arg(localNorm.gridSize);
            md += QStringLiteral("| Grid Interpolation | %1 |\n").arg(localNorm.gridInterpolation.toHtmlEscaped());
            md += QStringLiteral("| Local Scale Enabled | %1 |\n").arg(boolText(localNorm.localScaleEnabled));
            md += QStringLiteral("| Local Scale Smoothing Radius | %1 |\n").arg(localNorm.localScaleSmoothingRadius);
            md += QStringLiteral("| Evaluation Criteria | %1 |\n").arg(localNorm.evaluationCriteria.toHtmlEscaped());
            md += QStringLiteral("| Background Estimator | %1 |\n").arg(localNorm.backgroundEstimator.toHtmlEscaped());
            md +=
                QStringLiteral("| Background Percentile | %1 |\n").arg(formatStatValue(localNorm.backgroundPercentile));
            md += QStringLiteral("| Sigma Clip K | %1 |\n").arg(formatStatValue(localNorm.sigmaClipK));
            md += QStringLiteral("| Sigma Clip Iterations | %1 |\n").arg(localNorm.sigmaClipIterations);
            md += QStringLiteral("| Background Smoothing Radius | %1 |\n").arg(localNorm.backgroundSmoothingRadius);
            md += QStringLiteral("| Scale Evaluation | %1 |\n").arg(localNorm.scaleEvaluation.toHtmlEscaped());
            md += QStringLiteral("| Scale Mode | %1 |\n").arg(localNorm.scaleMode.toHtmlEscaped());
            md += QStringLiteral("| PSF Type | %1 |\n").arg(localNorm.psfType.toHtmlEscaped());
            md += QStringLiteral("| Moffat Beta | %1 |\n").arg(formatStatValue(localNorm.moffatBeta));
            md += QStringLiteral("| Max Stars | %1 |\n").arg(localNorm.maxStars);
            md += QStringLiteral("| Min SNR (flux scale) | %1 |\n").arg(formatStatValue(localNorm.minDetectionSnr));
            md += QStringLiteral("| Allow Clustered Sources | %1 |\n").arg(boolText(localNorm.allowClusteredSources));
            md += QStringLiteral("| Low Clip | %1 |\n").arg(formatStatValue(localNorm.lowClip));
            md += QStringLiteral("| High Clip | %1 |\n").arg(formatStatValue(localNorm.highClip));
            md += QStringLiteral("| Hot Pixel Median Radius | %1 |\n").arg(localNorm.hotPixelMedianRadius);
            md += QStringLiteral("| Noise Reduction Radius | %1 |\n").arg(localNorm.noiseReductionRadius);
            md += QStringLiteral("\n");
        }
        else if (normalizationEnabled && integrationGroup.stacking.normalizationMode == NormalizationMode::Local)
        {
            md += QStringLiteral(
                "_Local normalization is disabled for this group; local-normalization parameters are omitted._\n\n");
        }

        if (integrationGroup.drizzle.has_value())
        {
            md += QStringLiteral("### Drizzle\n\n");
            md += QStringLiteral("| Parameter | Value |\n");
            md += QStringLiteral("|---|---|\n");
            md += QStringLiteral("| Enabled | %1 |\n").arg(boolText(integrationGroup.drizzle->enabled));
            md += QStringLiteral("| Scale | %1 |\n").arg(formatStatValue(integrationGroup.drizzle->scale));
            md += QStringLiteral("| Pixel Fraction | %1 |\n")
                      .arg(formatStatValue(integrationGroup.drizzle->pixelFraction));
            md += QStringLiteral("| Kernel | %1 |\n").arg(integrationGroup.drizzle->kernel.toHtmlEscaped());
            md += QStringLiteral("| CFA Aware | %1 |\n").arg(boolText(integrationGroup.drizzle->cfaAware));
            md += QStringLiteral("| CFA Pattern | %1 |\n")
                      .arg(cfaPatternDisplayText(integrationGroup.drizzle->cfaPattern).toHtmlEscaped());
            md += QStringLiteral("\n");
        }

        if (registrationGroup == nullptr)
        {
            md += QStringLiteral("_No linked registration group found for this integration group._\n\n");
        }
        else
        {
            md += QStringLiteral("- **Registration Group:** %1\n\n")
                      .arg(friendlyGroupKey(registrationGroup->groupKey.toString()).toHtmlEscaped());
        }

        md += QStringLiteral("</details>\n\n");
    }
}

void WorkflowReportBuilder::appendAutoDecisions(QString& md) const
{
    appendSectionAnchor(md, QStringLiteral("auto-decisions"));
    md += QStringLiteral("## Auto Decisions\n");
    md += QStringLiteral("_Interpretation: every automatic setting recorded in the manifest is expanded here with "
                         "the resolved value, when the run left enough evidence to recover it._\n\n");

    QList<AutoDecisionRow> rows;
    const auto appendDecision = [&rows](QString area, QString group, QString requested, QString resolved,
                                        QString reason) {
        rows.append({std::move(area), std::move(group), std::move(requested), std::move(resolved), std::move(reason)});
    };

    const QString metricsDbPath = registrationMetricsDatabasePath(m_manifest, m_reportBaseDir);
    for (const auto& registrationGroup : m_manifest.registrationGroups)
    {
        const auto& reg = registrationGroup.settings;
        const QString groupName = friendlyGroupKey(registrationGroup.groupKey.toString());

        if (reg.autoReferenceDuringRegistration || reg.referenceImage.trimmed().isEmpty())
        {
            appendDecision(QStringLiteral("Registration Reference"), groupName, QStringLiteral("Automatic"),
                           registrationGroup.referenceFrame.has_value()
                               ? frameLabel(registrationGroup.referenceFrame.value())
                               : QStringLiteral("Automatic"),
                           QStringLiteral("Auto-reference selects the best available light from registration quality "
                                          "metrics when no manual reference is supplied"));
        }

        if (reg.transformMode.trimmed().compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0)
        {
            const AutoTransformResolution resolution =
                resolveAutoTransformDetail(registrationGroup, metricsDbPath, m_counters);
            appendDecision(QStringLiteral("Transform Model"), groupName, QStringLiteral("Auto"), resolution.detail,
                           resolution.reason);
        }

        if (isAutoToken(reg.interpolation))
        {
            appendDecision(QStringLiteral("Registration Interpolation"), groupName, QStringLiteral("Auto"),
                           QStringLiteral("Lanczos4"),
                           QStringLiteral("Auto resolves to Lanczos4 for high-quality sub-pixel resampling"));
        }

        if (isAutoToken(reg.cfaPattern))
        {
            appendDecision(QStringLiteral("Registration CFA Pattern"), groupName, QStringLiteral("Auto"),
                           QStringLiteral("from metadata"),
                           QStringLiteral("Auto reads the CFA pattern from image metadata at alignment time"));
        }

        if (isAutoToken(reg.debayerMethod))
        {
            appendDecision(QStringLiteral("Debayer Method"), groupName, QStringLiteral("Auto"),
                           QStringLiteral("engine-selected"),
                           QStringLiteral("Auto lets the demosaic engine choose the default method for the detected "
                                          "CFA data"));
        }
    }

    const auto appendStackingRejectionDecision = [&appendDecision](const QString& area, const QString& groupName,
                                                                   const StackingSettings& stacking, int frameCount) {
        if (stacking.requestedRejectionMethod.trimmed().compare(QStringLiteral("Auto"), Qt::CaseInsensitive) != 0)
        {
            return;
        }

        const QString effective =
            stacking.rejectionMethod.trimmed().isEmpty() ? QStringLiteral("None") : stacking.rejectionMethod.trimmed();
        appendDecision(area, groupName, QStringLiteral("Auto"), effective,
                       QStringLiteral("Auto resolved to %1 from the stacking policy for %2 frame(s)")
                           .arg(effective)
                           .arg(frameCount));
    };

    const auto appendCalibrationStageDecisions =
        [&appendStackingRejectionDecision](const std::optional<WorkflowManifest::CalibrationStage>& stage,
                                           const QString& type) {
            if (!stage.has_value())
            {
                return;
            }

            for (const auto& group : stage->groups)
            {
                appendStackingRejectionDecision(
                    QStringLiteral("Calibration Rejection"),
                    QStringLiteral("%1 %2").arg(type, friendlyGroupKey(group.groupKey.toString())), group.stacking,
                    static_cast<int>(group.inputFiles.size()));
            }
        };

    appendCalibrationStageDecisions(m_manifest.biasStage, QStringLiteral("Bias"));
    appendCalibrationStageDecisions(m_manifest.darkStage, QStringLiteral("Dark"));
    appendCalibrationStageDecisions(m_manifest.flatStage, QStringLiteral("Flat"));

    for (const auto& integrationGroup : m_manifest.integrationGroups)
    {
        const QString groupName = friendlyGroupKey(integrationGroup.groupKey.toString());
        appendStackingRejectionDecision(QStringLiteral("Stacking Rejection"), groupName, integrationGroup.stacking,
                                        static_cast<int>(integrationGroup.lightFiles.size()));

        const bool normalizationEnabled = integrationGroup.stacking.normalizationMode != NormalizationMode::None;
        const bool manualNormalizationReference =
            integrationGroup.stacking.normalizationReferenceMode == NormalizationReferenceMode::Manual &&
            !integrationGroup.stacking.manualNormalizationReference.trimmed().isEmpty();
        const bool explicitLocalReferenceStrategy = ImageIntegration::isExplicitLocalNormReferenceStrategy(
            integrationGroup.stacking.localNormalization.referenceStrategy);
        if (normalizationEnabled && !manualNormalizationReference && !explicitLocalReferenceStrategy)
        {
            QStringList refs;
            refs.reserve(static_cast<int>(integrationGroup.normalizationReferenceFrames.size()));
            for (const auto& frameId : integrationGroup.normalizationReferenceFrames)
            {
                const QString label = frameLabel(frameId);
                if (!label.trimmed().isEmpty() && label != QStringLiteral("-"))
                {
                    refs << label;
                }
            }
            refs.removeDuplicates();
            refs.sort(Qt::CaseInsensitive);
            appendDecision(QStringLiteral("Normalization Reference"), groupName, QStringLiteral("Automatic"),
                           refs.isEmpty() ? QStringLiteral("Automatic") : refs.join(QStringLiteral(", ")),
                           QStringLiteral("Auto-selected during normalization planning from frame quality and "
                                          "coverage"));
        }

        if (integrationGroup.drizzle.has_value() && integrationGroup.drizzle->cfaAware &&
            isAutoToken(integrationGroup.drizzle->cfaPattern))
        {
            appendDecision(QStringLiteral("Drizzle CFA Pattern"), groupName, QStringLiteral("Auto"),
                           QStringLiteral("from metadata"),
                           QStringLiteral("Auto reads the CFA pattern from image metadata before drizzle mapping"));
        }
    }

    if (rows.isEmpty())
    {
        md += QStringLiteral("_No automatic parameter decisions were recorded for this run._\n\n");
        return;
    }

    md += QStringLiteral("| Area | Group | Requested | Resolved | Why |\n");
    md += QStringLiteral("|---|---|---|---|---|\n");
    for (const auto& row : rows)
    {
        md += QStringLiteral("| %1 | %2 | %3 | %4 | %5 |\n")
                  .arg(row.area.toHtmlEscaped(), row.group.toHtmlEscaped(), row.requested.toHtmlEscaped(),
                       row.resolved.toHtmlEscaped(), row.reason.toHtmlEscaped());
    }
    md += QStringLiteral("\n");
}

void WorkflowReportBuilder::appendInputFiles(QString& md) const
{
    appendSectionAnchor(md, QStringLiteral("input-files"));
    md += QStringLiteral("## \U0001F4C1 Input Files\n\n");
    md += QStringLiteral("_Interpretation: source frame inventory. Exposures and Integration Time are derived from "
                         "light-frame metadata/filenames; ");
    md += QStringLiteral("`?s` means exposure could not be resolved._\n\n");

    // Summary table of light groups
    if (!m_manifest.integrationGroups.empty())
    {
        int totalLightFrames = 0;
        int totalIntegrationMs = 0;

        // Check if any group has resolvable exposure.
        bool hasExposure = false;
        for (const auto& group : m_manifest.integrationGroups)
        {
            if (buildExposureBreakdown(group).knownFrames > 0)
            {
                hasExposure = true;
                break;
            }
        }

        if (hasExposure)
        {
            md += QStringLiteral("| Group | Frames | Exposures | Integration Time |\n");
            md += QStringLiteral("|---|---|---|---|\n");
        }
        else
        {
            md += QStringLiteral("| Group | Frames |\n");
            md += QStringLiteral("|---|---|\n");
        }

        for (const auto& group : m_manifest.integrationGroups)
        {
            const ExposureBreakdown breakdown = buildExposureBreakdown(group);
            const int frames = breakdown.totalFrames;
            totalLightFrames += frames;

            if (hasExposure)
            {
                if (breakdown.knownFrames > 0)
                {
                    totalIntegrationMs += breakdown.totalMs;
                    QString exposureSummary = formatExposureList(breakdown.buckets);
                    if (breakdown.unknownFrames > 0)
                    {
                        exposureSummary += QStringLiteral(", %1 \u00D7 ?s").arg(breakdown.unknownFrames);
                    }

                    md += QStringLiteral("| %1 | %2 | %3 | %4 |\n")
                              .arg(friendlyGroupKey(group.groupKey.toString()))
                              .arg(frames)
                              .arg(exposureSummary)
                              .arg(formatDuration(breakdown.totalMs));
                }
                else
                {
                    md += QStringLiteral("| %1 | %2 | - | - |\n")
                              .arg(friendlyGroupKey(group.groupKey.toString()))
                              .arg(frames);
                }
            }
            else
            {
                md += QStringLiteral("| %1 | %2 |\n").arg(friendlyGroupKey(group.groupKey.toString())).arg(frames);
            }
        }

        if (hasExposure && totalIntegrationMs > 0)
        {
            md += QStringLiteral("| **Total** | **%1** | | **%2** |\n")
                      .arg(totalLightFrames)
                      .arg(formatDuration(totalIntegrationMs));
        }
        else
        {
            md += QStringLiteral("| **Total** | **%1** |\n").arg(totalLightFrames);
        }
        md += QStringLiteral("\n");
    }

    // Calibration frame counts summary
    int biasFrameCount = 0;
    int darkFrameCount = 0;
    int flatFrameCount = 0;
    if (m_manifest.biasStage.has_value())
    {
        for (const auto& g : m_manifest.biasStage->groups)
            biasFrameCount += static_cast<int>(g.inputFiles.size());
    }
    if (m_manifest.darkStage.has_value())
    {
        for (const auto& g : m_manifest.darkStage->groups)
            darkFrameCount += static_cast<int>(g.inputFiles.size());
    }
    if (m_manifest.flatStage.has_value())
    {
        for (const auto& g : m_manifest.flatStage->groups)
            flatFrameCount += static_cast<int>(g.inputFiles.size());
    }
    const int totalCalib = biasFrameCount + darkFrameCount + flatFrameCount;
    if (totalCalib > 0)
    {
        QStringList calibParts;
        if (biasFrameCount > 0)
            calibParts << QStringLiteral("%1 bias").arg(biasFrameCount);
        if (darkFrameCount > 0)
            calibParts << QStringLiteral("%1 dark").arg(darkFrameCount);
        if (flatFrameCount > 0)
            calibParts << QStringLiteral("%1 flat").arg(flatFrameCount);
        md += QStringLiteral("**Calibration frames**: %1 (%2)\n\n")
                  .arg(totalCalib)
                  .arg(calibParts.join(QStringLiteral(", ")));
    }

    md += QStringLiteral("_Detailed absolute file lists are available in Technical Appendix._\n\n");
}

void WorkflowReportBuilder::appendDetailedInputFileLists(QString& md) const
{
    const InputFileSets fileSets = collectInputFileSets(m_manifest);
    if (fileSets.isEmpty())
    {
        md += QStringLiteral("_No input files recorded._\n\n");
        return;
    }

    md += QStringLiteral("### \U0001F4C2 Full Input File Lists\n\n");

    auto appendFileListSection = [&](const QString& title, const QStringList& files) {
        if (files.isEmpty())
        {
            return;
        }

        QStringList sorted = files;
        sorted.sort(Qt::CaseInsensitive);

        md += QStringLiteral("<details>\n<summary>%1 (%2)</summary>\n\n").arg(title).arg(sorted.size());
        for (const auto& file : sorted)
        {
            md += QStringLiteral("- `%1`\n").arg(file);
        }
        md += QStringLiteral("\n</details>\n\n");
    };

    appendFileListSection(QStringLiteral("Light Frames"), fileSets.lightFiles.values());
    appendFileListSection(QStringLiteral("Bias Frames"), fileSets.biasFiles.values());
    appendFileListSection(QStringLiteral("Dark Frames"), fileSets.darkFiles.values());
    appendFileListSection(QStringLiteral("Flat Frames"), fileSets.flatFiles.values());
}

void WorkflowReportBuilder::appendWarnings(QString& md) const
{
    appendSectionAnchor(md, QStringLiteral("warnings-errors"));
    md += QStringLiteral("## \u26A0\uFE0F Warnings & Errors\n");

    if (m_result.warnings.empty() && m_result.errors.empty())
    {
        md += QStringLiteral("_No warnings or errors were recorded for this execution._\n\n");
        return;
    }

    md += QStringLiteral("<ul class=\"an-issues-list\">\n");
    for (const auto& err : m_result.errors)
    {
        md += QStringLiteral("<li class=\"an-issue-item an-issue-error\"><span class=\"an-severity-pill "
                             "an-severity-error\">ERROR</span> %1</li>\n")
                  .arg(err.toHtmlEscaped());
    }
    for (const auto& warn : m_result.warnings)
    {
        md += QStringLiteral("<li class=\"an-issue-item an-issue-warning\"><span class=\"an-severity-pill "
                             "an-severity-warning\">WARNING</span> %1</li>\n")
                  .arg(warn.toHtmlEscaped());
    }
    md += QStringLiteral("</ul>\n\n");
}

void WorkflowReportBuilder::appendTechnicalAppendix(QString& md) const
{
    const InputFileSets fileSets = collectInputFileSets(m_manifest);
    const bool hasFileLists = !fileSets.isEmpty();
    const bool hasExecutionLog = !m_result.capturedLog.trimmed().isEmpty();

    if (!hasFileLists && !hasExecutionLog)
    {
        return;
    }

    appendSectionAnchor(md, QStringLiteral("technical-appendix"));
    md += QStringLiteral("## \U0001F4CE Technical Appendix\n\n");

    if (hasFileLists)
    {
        appendDetailedInputFileLists(md);
    }
    if (hasExecutionLog)
    {
        appendExecutionLog(md);
    }
}

void WorkflowReportBuilder::appendExecutionLog(QString& md) const
{
    if (m_result.capturedLog.trimmed().isEmpty())
        return;

    md += QStringLiteral("### \U0001F4DC Execution Log\n\n");

    const GroupOutcomeCounts groupOutcomes = computeGroupOutcomeCounts(m_manifest, m_result);
    if (groupOutcomes.totalGroups > 0)
    {
        md += QStringLiteral("- **Group Outcomes**: Total %1 | Regenerated %2 | Reused %3 | Failed %4\n")
                  .arg(groupOutcomes.totalGroups)
                  .arg(groupOutcomes.regeneratedGroups)
                  .arg(groupOutcomes.reusedGroups)
                  .arg(groupOutcomes.failedGroups);
    }

    const LogInputTotals logInputTotals = parseStackingInputTotalsFromLog(m_result.capturedLog);
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

        md += QStringLiteral(
                  "- **Frame Retention (Log)**: Effective %1 (%2/%3) | Registered %4 (%5/%3) | Usable/Eff %6\n")
                  .arg(formatPercent(effectivePct))
                  .arg(logInputTotals.effective)
                  .arg(logInputTotals.lights)
                  .arg(formatPercent(registeredPct))
                  .arg(logInputTotals.registered)
                  .arg(formatPercent(usableOverEffectivePct));
    }

    const QStringList logStageSummary = logStageGroupSummaryParts(m_result.capturedLog);
    if (!logStageSummary.isEmpty())
    {
        md += QStringLiteral("- **Log Stage Groups**: %1\n").arg(logStageSummary.join(QStringLiteral(", ")));
    }

    if (groupOutcomes.totalGroups > 0 || !logStageSummary.isEmpty())
    {
        md += QStringLiteral("\n");
    }

    md += QStringLiteral("<details>\n<summary>Full execution log (%1 lines)</summary>\n\n")
              .arg(m_result.capturedLog.count(QLatin1Char('\n')) + 1);

    // Emit raw HTML: a styled <div> with syntax-highlighted log lines.
    // Each log line has the form: [2026-02-10T23:53:35.857] [LEVEL] message
    // We wrap the timestamp and level in <span> elements with CSS classes
    // defined in the HTML renderer (log-viewer, log-ts, log-header, etc.).
    static const QRegularExpression lineRx(
        QStringLiteral("^(\\[\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}\\.\\d+\\])\\s+\\[(\\w+)\\](.*)$"));

    md += QStringLiteral("<div class=\"log-viewer\">");

    const QStringList lines = m_result.capturedLog.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines)
    {
        // HTML-escape the whole line first to avoid injection
        const QString escaped = rawLine.toHtmlEscaped();

        // Try to parse the structured log format
        const auto match = lineRx.match(rawLine);
        if (match.hasMatch())
        {
            const QString ts = match.captured(1).toHtmlEscaped();
            const QString level = match.captured(2);
            const QString msg = match.captured(3).toHtmlEscaped();

            // Map log level to CSS class
            QString levelClass;
            if (level == QLatin1String("HEADER"))
                levelClass = QStringLiteral("log-header");
            else if (level == QLatin1String("DETAIL"))
                levelClass = QStringLiteral("log-detail");
            else if (level == QLatin1String("SUCCESS"))
                levelClass = QStringLiteral("log-success");
            else if (level == QLatin1String("WARNING"))
                levelClass = QStringLiteral("log-warning");
            else if (level == QLatin1String("ERROR"))
                levelClass = QStringLiteral("log-error");
            else
                levelClass = QStringLiteral("log-detail");

            // Detect stage header lines (═══ STAGE: ... ═══)
            const bool isStageHeader = msg.contains(QStringLiteral("\u2550\u2550\u2550"));

            if (isStageHeader)
            {
                md += QStringLiteral("<span class=\"log-ts\">%1</span> <span class=\"log-stage\">[%2]%3</span>\n")
                          .arg(ts, level.toHtmlEscaped(), msg);
            }
            else
            {
                md += QStringLiteral("<span class=\"log-ts\">%1</span> <span class=\"%2\">[%3]%4</span>\n")
                          .arg(ts, levelClass, level.toHtmlEscaped(), msg);
            }
        }
        else
        {
            // Unstructured line — emit as-is
            md += escaped;
            md += QLatin1Char('\n');
        }
    }

    md += QStringLiteral("</div>\n\n");
    md += QStringLiteral("</details>\n\n");
}

} // namespace ImageIntegration::Reporting
