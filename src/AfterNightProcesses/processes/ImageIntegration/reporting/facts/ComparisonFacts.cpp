// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ReportFacts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTimeZone>

#include <algorithm>
#include <utility>

/**
 * @file ComparisonFacts.cpp
 * @brief A run's totals beside those of the most recent earlier run of its output folder that completed.
 */

namespace ImageIntegration::Reporting
{
[[nodiscard]] std::vector<IntegrationGroupResult>
parseIntegrationGroupResultsFromArchiveJson(const QJsonArray& groupResultsArray)
{
    std::vector<IntegrationGroupResult> results;
    results.reserve(static_cast<size_t>(groupResultsArray.size()));

    for (const auto& value : groupResultsArray)
    {
        if (!value.isObject())
        {
            continue;
        }
        const QJsonObject obj = value.toObject();

        IntegrationGroupResult result;
        result.groupKey = obj.value(QStringLiteral("groupKey")).toString();
        result.outputPath = obj.value(QStringLiteral("outputPath")).toString();
        result.outputFilename = obj.value(QStringLiteral("outputFilename")).toString();
        result.outputBytes = obj.value(QStringLiteral("outputBytes")).toVariant().toLongLong();
        result.previewPath = obj.value(QStringLiteral("previewPath")).toString();
        result.outputRetentionStatus =
            outputRetentionStatusFromString(obj.value(QStringLiteral("outputRetentionStatus")).toString());
        result.outputRetentionReason = obj.value(QStringLiteral("outputRetentionReason")).toString();
        result.lightFrameCount = obj.value(QStringLiteral("lightFrameCount")).toInt();
        result.registrationGroupKey = obj.value(QStringLiteral("registrationGroupKey")).toString();
        result.registrationInputPolicy = obj.value(QStringLiteral("registrationInputPolicy")).toString();
        result.masterSolution = masterSolutionOutcomeFromString(obj.value(QStringLiteral("masterSolution")).toString());
        result.masterSolutionDetail = obj.value(QStringLiteral("masterSolutionDetail")).toString();
        result.durationMs = obj.value(QStringLiteral("durationMs")).toInt();
        result.finalizationTiming =
            stackGroupFinalizationTimingFromJson(obj.value(QStringLiteral("finalizationTiming")).toObject());
        result.qualityReport = stackingQualityReportFromJson(obj.value(QStringLiteral("qualityReport")).toObject());

        const QString resultState = obj.value(QStringLiteral("result")).toString().trimmed();
        result.success = resultState.compare(QStringLiteral("success"), Qt::CaseInsensitive) == 0;
        results.push_back(std::move(result));
    }

    return results;
}

[[nodiscard]] std::optional<ArchivedRunSnapshot> findPreviousSuccessfulRun(const QString& reportBaseDir)
{
    const QFileInfo baseInfo(reportBaseDir);
    const QDir currentRunDir = baseInfo.isDir() ? QDir(baseInfo.absoluteFilePath()) : baseInfo.dir();
    if (!currentRunDir.exists())
    {
        return std::nullopt;
    }

    QDir historyDir(currentRunDir);
    if (!historyDir.cdUp())
    {
        return std::nullopt;
    }

    const QString currentPath = QDir::cleanPath(currentRunDir.absolutePath());
    const QFileInfoList runDirs =
        historyDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
    for (auto it = runDirs.crbegin(); it != runDirs.crend(); ++it)
    {
        const QFileInfo& runInfo = *it;
        const QString runPath = QDir::cleanPath(runInfo.absoluteFilePath());
        if (runPath == currentPath)
        {
            continue;
        }

        const QString manifestPath = QDir(runPath).filePath(QStringLiteral("workflow_manifest.json"));
        QFile file(manifestPath);
        if (!file.exists() || !file.open(QIODevice::ReadOnly))
        {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            continue;
        }

        const QJsonObject root = doc.object();
        const QString overallResult = root.value(QStringLiteral("overallResult")).toString();
        if (overallResult.compare(QStringLiteral("success"), Qt::CaseInsensitive) != 0)
        {
            continue;
        }

        const QJsonObject manifestObject = root.value(QStringLiteral("manifest")).toObject();
        if (manifestObject.isEmpty())
        {
            continue;
        }

        ArchivedRunSnapshot snapshot;
        snapshot.executionDirName = runInfo.fileName();
        snapshot.manifestPath = manifestPath;
        snapshot.archivedAtLocal = root.value(QStringLiteral("archivedAtLocal")).toString().trimmed();
        snapshot.manifest = WorkflowManifest::fromJson(manifestObject);
        snapshot.integrationGroupResults = parseIntegrationGroupResultsFromArchiveJson(
            root.value(QStringLiteral("integrationGroupResults")).toArray());
        return snapshot;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<double> averageFiniteValues(const QList<double>& values)
{
    if (values.isEmpty())
    {
        return std::nullopt;
    }

    double total = 0.0;
    int count = 0;
    for (const auto value : values)
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
}

[[nodiscard]] RunDeltaMetrics
computeRunDeltaMetrics(const WorkflowManifest& manifest, const std::vector<IntegrationGroupResult>& groupResults,
                       const QString& reportBaseDir,
                       const std::function<std::optional<RawImageStats>(const QString&)>& analyzeImage)
{
    RunDeltaMetrics metrics;

    QHash<QString, const WorkflowManifest::IntegrationGroup*> integrationDefs;
    integrationDefs.reserve(static_cast<int>(manifest.integrationGroups.size()));
    for (const auto& group : manifest.integrationGroups)
    {
        integrationDefs.insert(group.groupKey.toString(), &group);
        const ExposureBreakdown breakdown = buildExposureBreakdown(group);
        metrics.totalLightFrames += breakdown.totalFrames;
        metrics.totalIntegrationMs += breakdown.totalMs;
    }

    QList<double> snrValues;
    QList<double> overlapValues;

    for (const auto& groupResult : groupResults)
    {
        if (!groupResult.success)
        {
            continue;
        }

        qint64 outputBytes = groupResult.outputBytes;
        if (outputBytes <= 0)
        {
            QString fallbackPath = resolvePathAgainstBase(groupResult.outputPath, reportBaseDir);
            if (fallbackPath.isEmpty() && !groupResult.outputFilename.trimmed().isEmpty())
            {
                fallbackPath = resolvePathAgainstBase(
                    QDir(manifest.outputDirectory).filePath(groupResult.outputFilename), reportBaseDir);
            }
            const QFileInfo fallbackInfo(fallbackPath);
            if (fallbackInfo.exists() && fallbackInfo.isFile())
            {
                outputBytes = fallbackInfo.size();
            }
        }
        if (outputBytes > 0)
        {
            metrics.totalOutputBytes += outputBytes;
        }

        QString statsPath = resolvePathAgainstBase(groupResult.outputPath, reportBaseDir);
        if (statsPath.isEmpty() && !groupResult.outputFilename.trimmed().isEmpty())
        {
            statsPath = resolvePathAgainstBase(QDir(manifest.outputDirectory).filePath(groupResult.outputFilename),
                                               reportBaseDir);
        }
        const auto stats = analyzeImage(statsPath);
        if (!stats.has_value())
        {
            continue;
        }

        const auto meanOpt = averageChannelStat(stats->channelStats, [](const RawChannelStats& c) { return c.mean; });
        const auto noiseOpt =
            averageChannelStat(stats->channelStats, [](const RawChannelStats& c) { return c.backgroundNoise; });
        if (meanOpt.has_value() && noiseOpt.has_value() && *noiseOpt > 0.0)
        {
            snrValues.append(*meanOpt / *noiseOpt);
        }

        const WorkflowManifest::IntegrationGroup* def = integrationDefs.value(groupResult.groupKey, nullptr);
        if (def == nullptr)
        {
            continue;
        }
        const auto sourceDims = parseResolutionDimensions(def->resolution);
        if (!sourceDims.has_value())
        {
            continue;
        }

        const double sourceArea = static_cast<double>(sourceDims->first) * static_cast<double>(sourceDims->second);
        const double outputArea = static_cast<double>(stats->width) * static_cast<double>(stats->height);
        if (sourceArea > 0.0 && outputArea > 0.0)
        {
            overlapValues.append(std::clamp(outputArea * 100.0 / sourceArea, 0.0, 100.0));
        }
    }

    metrics.avgSnrProxy = averageFiniteValues(snrValues);
    metrics.avgOverlapRetentionPct = averageFiniteValues(overlapValues);
    return metrics;
}

namespace
{
[[nodiscard]] ReportRunTotals totalsOf(const RunDeltaMetrics& metrics)
{
    ReportRunTotals totals;
    totals.frames = metrics.totalLightFrames;
    totals.integrationMs = metrics.totalIntegrationMs;
    totals.outputBytes = metrics.totalOutputBytes;
    totals.snr = metrics.avgSnrProxy;
    if (metrics.avgOverlapRetentionPct.has_value())
    {
        totals.overlapRetained = *metrics.avgOverlapRetentionPct / 100.0;
    }
    return totals;
}

/// When an earlier run's archive says it was written; nothing when it states no time that can be read.
[[nodiscard]] std::optional<QDateTime> archivedAtOf(const QString& archivedAtLocal)
{
    const QString text = archivedAtLocal.trimmed();
    if (text.isEmpty())
    {
        return std::nullopt;
    }
    QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!parsed.isValid())
    {
        parsed = QDateTime::fromString(text, Qt::ISODate);
    }
    return parsed.isValid() ? std::optional<QDateTime>(parsed) : std::nullopt;
}
} // namespace

void fillComparison(ReportBuild& build, ReportModel& model)
{
    const QString runDirectory = build.inputs().runDirectory;
    if (runDirectory.trimmed().isEmpty())
    {
        return;
    }
    const std::optional<ArchivedRunSnapshot> previous = findPreviousSuccessfulRun(runDirectory);
    if (!previous.has_value())
    {
        return;
    }

    const auto measure = [&build](const QString& path) { return build.measure(path); };
    ReportComparison comparison;
    comparison.baselineRunId = previous->executionDirName;
    comparison.baselineArchivedAt = archivedAtOf(previous->archivedAtLocal);
    comparison.current = totalsOf(
        computeRunDeltaMetrics(build.manifest(), build.result().integrationGroupResults, runDirectory, measure));
    comparison.previous = totalsOf(computeRunDeltaMetrics(previous->manifest, previous->integrationGroupResults,
                                                          QFileInfo(previous->manifestPath).absolutePath(), measure));
    model.comparison = std::move(comparison);
}
} // namespace ImageIntegration::Reporting
