// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file WorkflowReportBuilderHelpers.h
 * @brief The file-local helpers of WorkflowReportBuilder.cpp, shared by the section units it
 * was split into.
 *
 * Deliberately still an anonymous namespace: each of the three including units gets its own
 * copy, exactly as the one big unit had before the split, so nothing changes linkage or
 * behaviour. Include this only from the WorkflowReportBuilder*.cpp units.
 *
 * What is left here prints: the Markdown and the HTML fragments, and the figures read out of the
 * log. What reads a fact out of a run's records is in facts/ReportFacts.h.
 */
#pragma once

#include "ReportFormat.h"
#include "facts/ReportFacts.h"
#include "WorkflowReportBuilder.h"
#include "WorkflowReportSectionBuilders.h"

#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/SqliteConnection.h"
#include "processes/ImageIntegration/core/NormalizationUtils.h"
#include "processes/ImageIntegration/core/PhotometricWeightingUiUtils.h"
#include "processes/ImageIntegration/core/NormalizationOptionParsing.h"
#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"
#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/core/RejectionMapUtils.h"
#include "processes/ImageIntegration/core/StackWeightingInputs.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/model/FilterStatistics.h"
#include "processes/ImageIntegration/workflow/ManifestHashing.h"
#include "processes/ImageIntegration/workflow/stages/StageUtilities.h"
#include "processes/core/RegistrationMetricsStateLoader.h"

#include "controls/ImageStatisticsReport.h"
#include "controls/ImageViewFiles.h"
#include "widgets/DisplayNormalization.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Stacking/PhotometricWeighting.h>
#include <AfterNightEngine/Algorithms/Statistics/AdvancedStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Version.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecMetadataProjection.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

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
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
// How a group key is printed is the report format's, shared with the units that do not include this header.
using ImageIntegration::Reporting::friendlyGroupKey;
using ImageIntegration::Reporting::friendlyResolution;

// What reads a fact out of a run's records lives in the report model's units, which the report model is
// built from as well; the names are brought in for the helpers below.
using ImageIntegration::Reporting::addExposureBucket;
using ImageIntegration::Reporting::analyzeRawImage;
using ImageIntegration::Reporting::ArchivedRunSnapshot;
using ImageIntegration::Reporting::AutoTransformResolution;
using ImageIntegration::Reporting::averageChannelStat;
using ImageIntegration::Reporting::averageFiniteValues;
using ImageIntegration::Reporting::boolText;
using ImageIntegration::Reporting::buildExposureBreakdown;
using ImageIntegration::Reporting::canonicalOrRawPath;
using ImageIntegration::Reporting::cfaPatternDisplayText;
using ImageIntegration::Reporting::collectInputFileSets;
using ImageIntegration::Reporting::collectTargets;
using ImageIntegration::Reporting::CometReportRun;
using ImageIntegration::Reporting::computeGroupOutcomeCounts;
using ImageIntegration::Reporting::computeRunDeltaMetrics;
using ImageIntegration::Reporting::computeWeightsForSamples;
using ImageIntegration::Reporting::debayerMethodDisplayText;
using ImageIntegration::Reporting::ExposureBreakdown;
using ImageIntegration::Reporting::extractExposureFromFilename;
using ImageIntegration::Reporting::extractObservingDate;
using ImageIntegration::Reporting::extractSensorTemperature;
using ImageIntegration::Reporting::findPreviousSuccessfulRun;
using ImageIntegration::Reporting::formatIsoGainLabel;
using ImageIntegration::Reporting::formatLocationFromFits;
using ImageIntegration::Reporting::formatOffsetLabel;
using ImageIntegration::Reporting::frameLabel;
using ImageIntegration::Reporting::globalNormalizationMethodText;
using ImageIntegration::Reporting::GroupOutcomeCounts;
using ImageIntegration::Reporting::InputFileSets;
using ImageIntegration::Reporting::interpolationDisplayText;
using ImageIntegration::Reporting::isAutoToken;
using ImageIntegration::Reporting::isPolynomialTransformKind;
using ImageIntegration::Reporting::loadCometReportRuns;
using ImageIntegration::Reporting::loadRegistrationMetricsByFrame;
using ImageIntegration::Reporting::loadRegistrationMetricsForGroup;
using ImageIntegration::Reporting::loadRegistrationNoiseByFrame;
using ImageIntegration::Reporting::loadSpccHandoffSummaries;
using ImageIntegration::Reporting::medianOf;
using ImageIntegration::Reporting::metadataCameraModel;
using ImageIntegration::Reporting::metadataObjectName;
using ImageIntegration::Reporting::metadataStatesExposure;
using ImageIntegration::Reporting::metadataTelescope;
using ImageIntegration::Reporting::normalizationModeText;
using ImageIntegration::Reporting::normalizeMetadataText;
using ImageIntegration::Reporting::parseGroupExposure;
using ImageIntegration::Reporting::parseIntegrationGroupResultsFromArchiveJson;
using ImageIntegration::Reporting::parseResolutionDimensions;
using ImageIntegration::Reporting::probeReportHeader;
using ImageIntegration::Reporting::QualityWeightSample;
using ImageIntegration::Reporting::queryRegistrationSettingsVersion;
using ImageIntegration::Reporting::readFirstFitsValue;
using ImageIntegration::Reporting::recordedWeightsForSamples;
using ImageIntegration::Reporting::registrationBaseTransformDisplayName;
using ImageIntegration::Reporting::RegistrationMetricRow;
using ImageIntegration::Reporting::registrationMetricsDatabasePath;
using ImageIntegration::Reporting::registrationTransformDisplayName;
using ImageIntegration::Reporting::rejectionMapKindForTitle;
using ImageIntegration::Reporting::rejectionMethodText;
using ImageIntegration::Reporting::ReportExposureBucket;
using ImageIntegration::Reporting::ReportHeader;
using ImageIntegration::Reporting::reportProcessingProfileText;
using ImageIntegration::Reporting::reportProcessLabel;
using ImageIntegration::Reporting::reportTitle;
using ImageIntegration::Reporting::resolveAppVersion;
using ImageIntegration::Reporting::resolveAutoTransformDetail;
using ImageIntegration::Reporting::resolveContextValue;
using ImageIntegration::Reporting::resolveLightExposureSeconds;
using ImageIntegration::Reporting::resolvePathAgainstBase;
using ImageIntegration::Reporting::resolveTargetName;
using ImageIntegration::Reporting::RunDeltaMetrics;
using ImageIntegration::Reporting::scaleEstimatorText;
using ImageIntegration::Reporting::sha256Hex;
using ImageIntegration::Reporting::showsWeightFloor;
using ImageIntegration::Reporting::SpccHandoffGroupSummary;
using ImageIntegration::Reporting::stageDisplayName;
using ImageIntegration::Reporting::successfulTransformKinds;
using ImageIntegration::Reporting::transformKindDisplayName;
using ImageIntegration::Reporting::transformKindFromJson;
using ImageIntegration::Reporting::unquotedCardText;

struct AutoDecisionRow
{
    QString area;
    QString group;
    QString requested;
    QString resolved;
    QString reason;
};

using RawImageStats = ImageIntegration::Reporting::WorkflowReportRawImageStats;

[[nodiscard]] QString formatStatValue(double value)
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("-");
    }
    return QString::number(value, 'g', 10);
}

[[nodiscard]] QString formatSnrProxyValue(double value)
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("-");
    }
    return QString::number(value, 'f', 2);
}

[[nodiscard]] QString formatCompactDouble(double value, int decimals = 2)
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("-");
    }

    QString text = QString::number(value, 'f', decimals);
    while (text.contains(QLatin1Char('.')) && (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.'))))
    {
        text.chop(1);
    }
    return text;
}

using RawChannelStats = ImageIntegration::Reporting::WorkflowReportChannelStats;

/// A figure as the report prints it, or "n/a" when the measurement could not state it.
[[nodiscard]] QString formatOptionalStatValue(const std::optional<double>& value)
{
    return value.has_value() ? formatStatValue(*value) : QStringLiteral("n/a");
}

template <typename ValueFn>
[[nodiscard]] QString formatPerChannelStats(const std::vector<RawChannelStats>& stats, ValueFn&& valueFn)
{
    if (stats.empty())
    {
        return QStringLiteral("-");
    }

    if (stats.size() == 1)
    {
        return formatOptionalStatValue(valueFn(stats.front()));
    }

    QStringList parts;
    parts.reserve(static_cast<int>(stats.size()));
    for (size_t i = 0; i < stats.size(); ++i)
    {
        parts << QStringLiteral("C%1=%2").arg(i).arg(formatOptionalStatValue(valueFn(stats[i])));
    }
    return parts.join(QStringLiteral(", "));
}

[[nodiscard]] QString formatPerChannelCounts(const std::vector<RawChannelStats>& stats, bool unclipped)
{
    if (stats.empty())
    {
        return QStringLiteral("-");
    }

    const auto format_count = [](std::uint64_t value) { return QString::number(static_cast<qulonglong>(value)); };
    const auto countOf = [unclipped](const RawChannelStats& channel) {
        return unclipped ? channel.unclippedSamples : channel.samples;
    };

    if (stats.size() == 1)
    {
        return format_count(countOf(stats.front()));
    }

    QStringList parts;
    parts.reserve(static_cast<int>(stats.size()));
    for (size_t i = 0; i < stats.size(); ++i)
    {
        parts << QStringLiteral("C%1=%2").arg(i).arg(format_count(countOf(stats[i])));
    }
    return parts.join(QStringLiteral(", "));
}

[[nodiscard]] QString buildMediaCardHtml(const QString& title, const QString& relativePath,
                                         const std::optional<RawImageStats>& statsOpt)
{
    const QString safeTitle = title.toHtmlEscaped();
    const QString safePath = relativePath.toHtmlEscaped();

    QString html;
    QTextStream out(&html);
    out << "<div class=\"an-media-card\">";
    out << "<div class=\"an-media-grid\">";
    out << "<figure class=\"an-figure\">";
    out << "<a href=\"" << safePath << "\" target=\"_blank\" rel=\"noopener\">";
    out << "<img src=\"" << safePath << "\" alt=\"" << safeTitle << "\" loading=\"lazy\"/>";
    out << "</a>";
    out << "<figcaption class=\"an-figcap\">" << safeTitle << "</figcaption>";
    out << "</figure>";

    out << "<div>";
    out << "<div class=\"an-meta-title\">Image statistics</div>";

    if (statsOpt.has_value())
    {
        const auto& st = statsOpt.value();
        out << "<table class=\"an-stats-table\">";
        out << "<tr><td>Dimensions</td><td>"
            << QStringLiteral("%1×%2")
                   .arg(static_cast<qulonglong>(st.width))
                   .arg(static_cast<qulonglong>(st.height))
                   .toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>Channels</td><td>" << QString::number(static_cast<qulonglong>(st.channels)).toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>Samples (clipped)</td><td>" << formatPerChannelCounts(st.channelStats, false).toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>Samples (unclipped)</td><td>" << formatPerChannelCounts(st.channelStats, true).toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>Min</td><td>"
            << formatPerChannelStats(st.channelStats, [](const RawChannelStats& c) { return c.minimum; })
                   .toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>Max</td><td>"
            << formatPerChannelStats(st.channelStats, [](const RawChannelStats& c) { return c.maximum; })
                   .toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>Mean</td><td>"
            << formatPerChannelStats(st.channelStats, [](const RawChannelStats& c) { return c.mean; }).toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>Median</td><td>"
            << formatPerChannelStats(st.channelStats, [](const RawChannelStats& c) { return c.median; }).toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>Sigma</td><td>"
            << formatPerChannelStats(st.channelStats, [](const RawChannelStats& c) { return c.standardDeviation; })
                   .toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>Std Dev</td><td>"
            << formatPerChannelStats(st.channelStats, [](const RawChannelStats& c) { return c.standardDeviation; })
                   .toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>Avg Dev</td><td>"
            << formatPerChannelStats(st.channelStats, [](const RawChannelStats& c) { return c.averageDeviation; })
                   .toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>Variance</td><td>"
            << formatPerChannelStats(st.channelStats, [](const RawChannelStats& c) { return c.variance; })
                   .toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>MAD</td><td>"
            << formatPerChannelStats(st.channelStats,
                                     [](const RawChannelStats& c) { return c.medianAbsoluteDeviation; })
                   .toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>BG Noise</td><td>"
            << formatPerChannelStats(st.channelStats, [](const RawChannelStats& c) { return c.backgroundNoise; })
                   .toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>BWMV</td><td>"
            << formatPerChannelStats(st.channelStats, [](const RawChannelStats& c) { return c.biweightMidvariance; })
                   .toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>ASQR</td><td>"
            << formatPerChannelStats(st.channelStats, [](const RawChannelStats& c) { return c.meanOfSquares; })
                   .toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>SSQR</td><td>"
            << formatPerChannelStats(st.channelStats, [](const RawChannelStats& c) { return c.sumOfSquares; })
                   .toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>IKSS Location</td><td>"
            << formatPerChannelStats(st.channelStats, [](const RawChannelStats& c) { return c.ikssLocation; })
                   .toHtmlEscaped()
            << "</td></tr>";
        out << "<tr><td>IKSS Scale</td><td>"
            << formatPerChannelStats(st.channelStats, [](const RawChannelStats& c) { return c.ikssScale; })
                   .toHtmlEscaped()
            << "</td></tr>";
        out << "</table>";
    }
    else
    {
        out << "<div class=\"an-figcap\">(No raw image statistics available)</div>";
    }

    out << "</div>";
    out << "</div>";
    out << "</div>";
    return html;
}

[[nodiscard]] QString buildSecondaryImageCardHtml(const QString& title, const QString& relativePath,
                                                  const QString& subtitle)
{
    const QString safeTitle = title.toHtmlEscaped();
    const QString safePath = relativePath.toHtmlEscaped();
    const QString safeSubtitle = subtitle.toHtmlEscaped();

    QString html;
    QTextStream out(&html);
    out << "<div class=\"an-secondary-card\">";
    out << "<figure class=\"an-figure\">";
    out << "<a href=\"" << safePath << "\" target=\"_blank\" rel=\"noopener\">";
    out << "<img src=\"" << safePath << "\" alt=\"" << safeTitle << "\" loading=\"lazy\"/>";
    out << "</a>";
    out << "<figcaption class=\"an-figcap\">" << safeTitle;
    if (!safeSubtitle.isEmpty())
    {
        out << "<br/>" << safeSubtitle;
    }
    out << "</figcaption>";
    out << "</figure>";
    out << "</div>";
    return html;
}

[[nodiscard]] QString formatExposureSeconds(double seconds, int precision = 3)
{
    QString text = QString::number(seconds, 'f', precision);
    while (text.contains(QLatin1Char('.')) && (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.'))))
    {
        text.chop(1);
    }
    return text;
}

[[nodiscard]] QString formatExposureList(const QList<ReportExposureBucket>& buckets)
{
    if (buckets.isEmpty())
    {
        return QStringLiteral("-");
    }

    QStringList parts;
    parts.reserve(buckets.size());
    for (const auto& bucket : buckets)
    {
        parts << QStringLiteral("%1 \u00D7 %2s")
                     .arg(bucket.frameCount)
                     .arg(formatExposureSeconds(bucket.exposureSeconds));
    }
    return parts.join(QStringLiteral(", "));
}

[[nodiscard]] QString canonicalStageKey(const QString& stageName)
{
    QString key;
    key.reserve(stageName.size());
    for (const QChar ch : stageName)
    {
        if (ch.isLetterOrNumber())
        {
            key.append(ch.toLower());
        }
    }
    return key;
}

[[nodiscard]] QHash<QString, int> logStageGroupCountMap(const QString& capturedLog)
{
    QHash<QString, int> map;
    if (capturedLog.trimmed().isEmpty())
    {
        return map;
    }

    static const QRegularExpression stageHeaderRx(
        QStringLiteral("^\\s*\\x{2550}+\\s*STAGE:\\s*(.+?)\\s*\\((\\d+)\\s+groups?\\)\\s*\\x{2550}+\\s*$"));

    const QStringList lines = capturedLog.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines)
    {
        const auto match = stageHeaderRx.match(rawLine.trimmed());
        if (!match.hasMatch())
        {
            continue;
        }

        bool ok = false;
        const int groups = match.captured(2).toInt(&ok);
        if (!ok || groups < 0)
        {
            continue;
        }

        const QString key = canonicalStageKey(match.captured(1));
        if (!key.isEmpty())
        {
            map.insert(key, groups);
        }
    }

    return map;
}

[[nodiscard]] QStringList logStageGroupSummaryParts(const QString& capturedLog)
{
    QStringList parts;
    if (capturedLog.trimmed().isEmpty())
    {
        return parts;
    }

    static const QRegularExpression stageHeaderRx(
        QStringLiteral("^\\s*\\x{2550}+\\s*STAGE:\\s*(.+?)\\s*\\((\\d+)\\s+groups?\\)\\s*\\x{2550}+\\s*$"));

    QSet<QString> seenStageKeys;
    const QStringList lines = capturedLog.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines)
    {
        const auto match = stageHeaderRx.match(rawLine.trimmed());
        if (!match.hasMatch())
        {
            continue;
        }

        const QString key = canonicalStageKey(match.captured(1));
        if (key.isEmpty() || seenStageKeys.contains(key))
        {
            continue;
        }

        bool ok = false;
        const int groups = match.captured(2).toInt(&ok);
        if (!ok || groups < 0)
        {
            continue;
        }

        seenStageKeys.insert(key);
        parts << QStringLiteral("%1=%2").arg(stageDisplayName(match.captured(1).trimmed())).arg(groups);
    }

    return parts;
}

struct LogInputTotals
{
    int lights = 0;
    int effective = 0;
    int registered = 0;
};

[[nodiscard]] LogInputTotals parseStackingInputTotalsFromLog(const QString& capturedLog)
{
    LogInputTotals totals;
    if (capturedLog.trimmed().isEmpty())
    {
        return totals;
    }

    static const QRegularExpression inputsRx(
        QStringLiteral("Inputs:\\s*Lights:\\s*(\\d+)\\s*\\|\\s*Effective:\\s*(\\d+)\\s*\\|\\s*Registered:\\s*(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);

    const QStringList lines = capturedLog.split(QLatin1Char('\n'));
    for (const QString& line : lines)
    {
        const auto match = inputsRx.match(line);
        if (!match.hasMatch())
        {
            continue;
        }

        bool okLights = false;
        bool okEffective = false;
        bool okRegistered = false;
        const int lights = match.captured(1).toInt(&okLights);
        const int effective = match.captured(2).toInt(&okEffective);
        const int registered = match.captured(3).toInt(&okRegistered);

        if (okLights && okEffective && okRegistered)
        {
            totals.lights += (std::max)(0, lights);
            totals.effective += (std::max)(0, effective);
            totals.registered += (std::max)(0, registered);
        }
    }

    return totals;
}

[[nodiscard]] QString formatPercent(double value)
{
    if (!std::isfinite(value))
    {
        return QStringLiteral("-");
    }
    return QString::number(value, 'f', 1) + QStringLiteral("%");
}

enum class QualityGateState
{
    Pass,
    Warn,
    Fail
};

[[nodiscard]] QualityGateState qualityGateStateFromPercent(double value, double passThreshold, double warnThreshold)
{
    if (!std::isfinite(value))
    {
        return QualityGateState::Warn;
    }
    if (value >= passThreshold)
    {
        return QualityGateState::Pass;
    }
    if (value >= warnThreshold)
    {
        return QualityGateState::Warn;
    }
    return QualityGateState::Fail;
}

[[nodiscard]] QString qualityGateBadge(QualityGateState state)
{
    switch (state)
    {
    case QualityGateState::Pass:
        return QStringLiteral("<span class=\"an-quality-badge an-quality-pass\">PASS</span>");
    case QualityGateState::Warn:
        return QStringLiteral("<span class=\"an-quality-badge an-quality-warn\">WARN</span>");
    case QualityGateState::Fail:
    default:
        return QStringLiteral("<span class=\"an-quality-badge an-quality-fail\">FAIL</span>");
    }
}

struct WeightedFrameContribution
{
    QString frameId;
    QString frameLabel;
    double exposureSeconds = 0.0;
    double weight = 0.0;
    std::optional<double> qualityScore;
    double weightedSeconds = 0.0;
};

struct GroupQualityWeightSummary
{
    QString groupName;
    QString weightingMode;
    double minWeightFloor = 0.0;
    int totalFrames = 0;
    int framesWithMetrics = 0;
    int framesSucceeded = 0;
    int framesAtMinFloor = 0;
    int weightedIntegrationMs = 0;
    std::optional<double> medianWeight;
    QList<double> weights;
    QList<double> qualityScores;
    QList<double> zeroPoints;
    QList<double> astrometricRmsArcsec;
    QList<double> catalogMatchedStars;
    QList<WeightedFrameContribution> contributions;
};

[[nodiscard]] QString compactManifestId(QString value)
{
    value = value.trimmed();
    return value.isEmpty() ? QStringLiteral("-") : value;
}

[[nodiscard]] QString formatOptionalCompactDouble(const std::optional<double>& value, const QString& suffix)
{
    if (!value.has_value())
    {
        return QStringLiteral("-");
    }
    return QStringLiteral("%1 %2").arg(formatCompactDouble(*value), suffix);
}

[[nodiscard]] QString markdownTableCell(QString value)
{
    value = value.trimmed();
    if (value.isEmpty())
    {
        return QStringLiteral("-");
    }

    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    value.replace(QLatin1Char('|'), QStringLiteral("\\|"));
    return value.toHtmlEscaped();
}

[[nodiscard]] QString cometDisplayToken(QString token)
{
    token = token.trimmed();
    if (token.isEmpty())
    {
        return QStringLiteral("-");
    }

    if (token == QStringLiteral("AcceptedAutomatic"))
    {
        return QStringLiteral("Accepted automatic");
    }
    if (token == QStringLiteral("AutoCandidate"))
    {
        return QStringLiteral("Automatic candidate");
    }
    if (token == QStringLiteral("PredictedOnly"))
    {
        return QStringLiteral("Predicted-only");
    }
    if (token == QStringLiteral("comet_aligned"))
    {
        return QStringLiteral("comet_aligned");
    }
    return token;
}

[[nodiscard]] QString cometInputSummaryText(const std::optional<an::ui::CometTrackingRunSummaryRecord>& summary)
{
    if (!summary.has_value())
    {
        return QStringLiteral("-");
    }

    return QStringLiteral("Accepted %1 / Rejected %2 / Predicted-only %3")
        .arg(summary->acceptedInputCount)
        .arg(summary->rejectedInputCount)
        .arg(summary->predictedOnlyFrameCount);
}

[[nodiscard]] QString cometPointSummaryText(const std::vector<an::ui::CometTrackingPointRecord>& points)
{
    int manualCount = 0;
    int acceptedAutomaticCount = 0;
    int predictedOnlyCount = 0;
    int outlierCount = 0;

    for (const auto& point : points)
    {
        if (point.origin == QStringLiteral("Manual"))
        {
            ++manualCount;
        }
        else if (point.origin == QStringLiteral("AcceptedAutomatic"))
        {
            ++acceptedAutomaticCount;
        }

        if (point.fitState == QStringLiteral("PredictedOnly"))
        {
            ++predictedOnlyCount;
        }
        else if (point.fitState == QStringLiteral("Outlier"))
        {
            ++outlierCount;
        }
    }

    return QStringLiteral("Manual %1 / Accepted automatic %2 / Predicted-only %3 / Outliers %4")
        .arg(manualCount)
        .arg(acceptedAutomaticCount)
        .arg(predictedOnlyCount)
        .arg(outlierCount);
}

[[nodiscard]] QString cometReadinessText(const std::optional<an::ui::CometTransformRoleProduct>& product)
{
    if (!product.has_value())
    {
        return QStringLiteral("-");
    }

    const an::ui::CometRoleReadinessRecord& readiness = product->readiness;
    QStringList parts;
    parts << cometDisplayToken(readiness.readinessStatus);
    parts << QStringLiteral("%1 selected").arg(readiness.selectedFrameCount);
    parts << QStringLiteral("%1 ok / %2 failed / %3 missing")
                 .arg(readiness.successfulTransformCount)
                 .arg(readiness.failedTransformCount)
                 .arg(readiness.missingTransformCount);
    if (!readiness.outputPlaneStatus.trimmed().isEmpty())
    {
        parts << readiness.outputPlaneStatus.trimmed();
    }
    if (!readiness.failureReason.trimmed().isEmpty())
    {
        parts << readiness.failureReason.trimmed();
    }
    return parts.join(QStringLiteral("; "));
}

[[nodiscard]] QString cometOutputPlaneText(const std::optional<an::ui::CometTransformRoleProduct>& product)
{
    if (!product.has_value())
    {
        return QStringLiteral("-");
    }

    const an::ui::CometOutputPlaneRecord& plane = product->outputPlane;
    QStringList parts;
    if (!plane.coordinateFrameKind.trimmed().isEmpty())
    {
        parts << plane.coordinateFrameKind.trimmed();
    }
    if (!plane.alignmentMode.trimmed().isEmpty())
    {
        parts << plane.alignmentMode.trimmed();
    }
    if (plane.canvasWidth > 0 && plane.canvasHeight > 0)
    {
        parts << QStringLiteral("%1x%2").arg(plane.canvasWidth).arg(plane.canvasHeight);
    }
    if (plane.skyWcsValid)
    {
        parts << QStringLiteral("sidereal WCS");
    }
    else if (!plane.skyWcsInvalidReason.trimmed().isEmpty())
    {
        parts << plane.skyWcsInvalidReason.trimmed();
    }
    else
    {
        parts << QStringLiteral("non-sidereal plane");
    }
    return parts.join(QStringLiteral("; "));
}

[[nodiscard]] QString cometTransformSummaryText(const std::optional<an::ui::CometTransformRoleProduct>& product)
{
    if (!product.has_value())
    {
        return QStringLiteral("-");
    }

    const an::ui::CometRoleReadinessRecord& readiness = product->readiness;
    if (readiness.selectedFrameCount > 0 || readiness.successfulTransformCount > 0 ||
        readiness.failedTransformCount > 0 || readiness.missingTransformCount > 0)
    {
        return QStringLiteral("%1 ok / %2 failed / %3 missing")
            .arg(readiness.successfulTransformCount)
            .arg(readiness.failedTransformCount)
            .arg(readiness.missingTransformCount);
    }

    int succeededCount = 0;
    int failedCount = 0;
    for (const auto& transform : product->transforms)
    {
        if (transform.succeeded)
        {
            ++succeededCount;
        }
        else
        {
            ++failedCount;
        }
    }
    return QStringLiteral("%1 ok / %2 failed").arg(succeededCount).arg(failedCount);
}

[[nodiscard]] QString statusBreakdownText(const SpccHandoffGroupSummary& summary)
{
    QStringList parts;
    const auto appendCount = [&parts](const QString& label, const int count) {
        if (count > 0)
        {
            parts << QStringLiteral("%1: %2").arg(label).arg(count);
        }
    };

    appendCount(QStringLiteral("missing source"), summary.missingSourceCount);
    appendCount(QStringLiteral("missing chunk/cache"), summary.missingChunkOrCacheCount);
    appendCount(QStringLiteral("manifest mismatch"), summary.manifestMismatchCount);
    appendCount(QStringLiteral("stale match"), summary.staleMatchCount);
    appendCount(QStringLiteral("quality rejected"), summary.rejectedQualityFieldsCount);
    appendCount(QStringLiteral("untested"), summary.untestedCount);
    return parts.isEmpty() ? QStringLiteral("none") : parts.join(QStringLiteral("; "));
}

[[nodiscard]] std::vector<int> buildHistogram(const QList<double>& values, double minValue, double maxValue,
                                              int binCount)
{
    std::vector<int> bins(static_cast<size_t>((std::max)(1, binCount)), 0);
    if (values.isEmpty() || !(maxValue > minValue))
    {
        return bins;
    }

    const int safeBins = static_cast<int>(bins.size());
    for (double value : values)
    {
        if (!std::isfinite(value))
        {
            continue;
        }
        const double clamped = std::clamp(value, minValue, maxValue);
        int binIndex = static_cast<int>(((clamped - minValue) / (maxValue - minValue)) * safeBins);
        if (binIndex >= safeBins)
        {
            binIndex = safeBins - 1;
        }
        if (binIndex < 0)
        {
            binIndex = 0;
        }
        bins[static_cast<size_t>(binIndex)]++;
    }
    return bins;
}

[[nodiscard]] std::pair<double, double> observedRange(const QList<double>& values)
{
    double minValue = std::numeric_limits<double>::infinity();
    double maxValue = -std::numeric_limits<double>::infinity();
    for (const double value : values)
    {
        if (!std::isfinite(value))
        {
            continue;
        }
        minValue = (std::min)(minValue, value);
        maxValue = (std::max)(maxValue, value);
    }

    if (!std::isfinite(minValue) || !std::isfinite(maxValue))
    {
        return {0.0, 1.0};
    }

    if (maxValue <= minValue)
    {
        const double padding = (std::max)(0.05, std::abs(minValue) * 0.1);
        return {(std::max)(0.0, minValue - padding), maxValue + padding};
    }

    return {minValue, maxValue};
}

[[nodiscard]] QString buildHistogramCardHtml(const QString& title, const QString& subtitle,
                                             const std::vector<int>& bins, const QString& minLabel,
                                             const QString& maxLabel, std::optional<int> markerBin)
{
    int maxCount = 0;
    for (const int count : bins)
    {
        maxCount = (std::max)(maxCount, count);
    }
    if (maxCount <= 0)
    {
        maxCount = 1;
    }

    QString html;
    QTextStream out(&html);
    out << "<div class=\"an-quality-hist-card\">";
    out << "<div class=\"an-quality-hist-title\">" << title.toHtmlEscaped() << "</div>";
    out << "<div class=\"an-quality-hist-subtitle\">" << subtitle.toHtmlEscaped() << "</div>";
    out << "<div class=\"an-quality-hist-bars\">";
    for (int index = 0; index < static_cast<int>(bins.size()); ++index)
    {
        const int count = bins[static_cast<size_t>(index)];
        const double heightPct =
            count > 0 ? std::clamp(static_cast<double>(count) * 100.0 / static_cast<double>(maxCount), 4.0, 100.0)
                      : 0.0;
        const bool marked = markerBin.has_value() && *markerBin == index;
        out << "<div class=\"an-quality-hist-bar-wrap\" title=\"Bin " << (index + 1) << ": " << count << " frame(s)\">";
        out << "<span class=\"an-quality-hist-bar" << (marked ? " is-marker" : "")
            << "\" style=\"height:" << QString::number(heightPct, 'f', 2) << "%\"></span>";
        out << "</div>";
    }
    out << "</div>";
    out << "<div class=\"an-quality-hist-axis\"><span>" << minLabel.toHtmlEscaped() << "</span><span>"
        << maxLabel.toHtmlEscaped() << "</span></div>";
    out << "</div>";
    return html;
}

[[nodiscard]] QString stageSeverityClass(const QString& stageStatus)
{
    const QString normalized = stageStatus.trimmed().toLower();
    if (normalized == QStringLiteral("failed"))
    {
        return QStringLiteral("an-severity-error");
    }
    if (normalized == QStringLiteral("partial") || normalized == QStringLiteral("cancelled"))
    {
        return QStringLiteral("an-severity-warning");
    }
    if (normalized == QStringLiteral("completed") || normalized == QStringLiteral("success"))
    {
        return QStringLiteral("an-severity-success");
    }
    return QStringLiteral("an-severity-info");
}

[[nodiscard]] QString inferLikelyFailureCategory(const WorkflowExecutor::Result& result)
{
    if (result.cancelled)
    {
        return QStringLiteral("User cancellation");
    }

    QStringList signalTexts;
    signalTexts.reserve(result.errors.size() + result.warnings.size() + result.stageResults.size());
    for (const auto& error : result.errors)
    {
        signalTexts << error.toLower();
    }
    for (const auto& warning : result.warnings)
    {
        signalTexts << warning.toLower();
    }
    for (const auto& stage : result.stageResults)
    {
        if (stage.status.compare(QStringLiteral("failed"), Qt::CaseInsensitive) == 0 ||
            stage.status.compare(QStringLiteral("partial"), Qt::CaseInsensitive) == 0 ||
            stage.status.compare(QStringLiteral("cancelled"), Qt::CaseInsensitive) == 0)
        {
            signalTexts << stage.stageName.toLower();
        }
    }

    const QString all = signalTexts.join(QLatin1Char(' '));
    if (all.contains(QStringLiteral("file")) || all.contains(QStringLiteral("path")) ||
        all.contains(QStringLiteral("directory")) || all.contains(QStringLiteral("not found")) ||
        all.contains(QStringLiteral("read")) || all.contains(QStringLiteral("write")) ||
        all.contains(QStringLiteral("permission")) || all.contains(QStringLiteral("disk")))
    {
        return QStringLiteral("I/O or file access");
    }
    if (all.contains(QStringLiteral("memory")) || all.contains(QStringLiteral("exhaust")))
    {
        return QStringLiteral("Resource exhaustion");
    }
    if (all.contains(QStringLiteral("timeout")))
    {
        return QStringLiteral("Timeout");
    }
    if (all.contains(QStringLiteral("invalid")) || all.contains(QStringLiteral("incompatible")) ||
        all.contains(QStringLiteral("missing")) || all.contains(QStringLiteral("validation")))
    {
        return QStringLiteral("Validation or configuration mismatch");
    }
    if (all.contains(QStringLiteral("registration")) || all.contains(QStringLiteral("alignment")))
    {
        return QStringLiteral("Registration pipeline");
    }
    if (all.contains(QStringLiteral("calibration")) || all.contains(QStringLiteral("bias")) ||
        all.contains(QStringLiteral("dark")) || all.contains(QStringLiteral("flat")))
    {
        return QStringLiteral("Calibration pipeline");
    }
    if (all.contains(QStringLiteral("normalization")))
    {
        return QStringLiteral("Normalization pipeline");
    }
    if (all.contains(QStringLiteral("stack")) || all.contains(QStringLiteral("drizzle")))
    {
        return QStringLiteral("Stacking or drizzle pipeline");
    }

    return QStringLiteral("Unknown or mixed causes");
}

[[nodiscard]] QString compactExecutionStatus(const WorkflowExecutor::Result& result)
{
    if (!result.success && result.cancelled)
    {
        return QStringLiteral("Cancelled");
    }
    if (!result.success)
    {
        return QStringLiteral("Failed");
    }
    return QStringLiteral("Success");
}

[[nodiscard]] QString compactExecutionStatusWithIcon(const WorkflowExecutor::Result& result)
{
    if (!result.success && result.cancelled)
    {
        return QStringLiteral("\u21BA Cancelled");
    }
    if (!result.success)
    {
        return QStringLiteral("\u274C Failed");
    }
    return QStringLiteral("\u2705 Success");
}

[[nodiscard]] QString executionStatusBadgeClass(const WorkflowExecutor::Result& result)
{
    if (!result.success && result.cancelled)
    {
        return QStringLiteral("is-cancelled");
    }
    if (!result.success)
    {
        return QStringLiteral("is-failed");
    }
    return QStringLiteral("is-success");
}

[[nodiscard]] QString formatHumanDateTime(const QString& dateTimeText, const QDateTime& fallback)
{
    const QString trimmed = dateTimeText.trimmed();
    if (trimmed.isEmpty())
    {
        return fallback.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }

    QDateTime parsed = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
    if (!parsed.isValid())
    {
        parsed = QDateTime::fromString(trimmed, Qt::ISODate);
    }
    if (!parsed.isValid())
    {
        parsed = QDateTime::fromString(trimmed, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    if (!parsed.isValid())
    {
        return trimmed;
    }
    return parsed.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

[[nodiscard]] QString loadLogoDataUri(const QString& path)
{
    QImage image(path);
    if (image.isNull())
    {
        return {};
    }

    const QImage scaled = image.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly))
    {
        return {};
    }
    if (!scaled.save(&buffer, "PNG", 75))
    {
        return {};
    }
    return QStringLiteral("data:image/png;base64,%1").arg(QString::fromLatin1(bytes.toBase64()));
}

[[nodiscard]] QString findAfterNightLogoPath()
{
    static const QStringList kResourceLogoCandidates = {
        QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_256x256_blue.png"),
        QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_128x128_blue.png"),
        QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_512x512_blue.png"),
        QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_924x924_blue.png")};
    for (const auto& resourcePath : kResourceLogoCandidates)
    {
        QFile resourceFile(resourcePath);
        if (resourceFile.exists())
        {
            return resourcePath;
        }
    }

    static const QStringList kFilesystemLogoCandidates = {QStringLiteral("icons/afternight_icon_256x256_blue.png"),
                                                          QStringLiteral("icons/afternight_icon_128x128_blue.png"),
                                                          QStringLiteral("icons/afternight_icon_512x512_blue.png")};

    auto resolveFromRoot = [&](const QString& rootPath) -> QString {
        if (rootPath.trimmed().isEmpty())
        {
            return {};
        }
        QDir root(rootPath);
        for (const auto& relative : kFilesystemLogoCandidates)
        {
            const QString candidate = root.filePath(relative);
            if (QFileInfo::exists(candidate))
            {
                return candidate;
            }
        }
        return {};
    };

    if (const QString direct = resolveFromRoot(QDir::currentPath()); !direct.isEmpty())
    {
        return direct;
    }

    QDir appDir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i)
    {
        if (const QString found = resolveFromRoot(appDir.path()); !found.isEmpty())
        {
            return found;
        }
        if (!appDir.cdUp())
        {
            break;
        }
    }

    return {};
}

[[nodiscard]] QString buildLogoHtml()
{
    const QString logoPath = findAfterNightLogoPath();
    if (!logoPath.isEmpty())
    {
        const QString dataUri = loadLogoDataUri(logoPath);
        if (!dataUri.isEmpty())
        {
            return QStringLiteral("<img src=\"%1\" alt=\"AfterNight logo\"/>").arg(dataUri);
        }
    }

    return QStringLiteral(
        "<svg viewBox=\"0 0 36 36\" xmlns=\"http://www.w3.org/2000/svg\" role=\"img\">"
        "<defs><linearGradient id=\"anLogoGrad\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">"
        "<stop offset=\"0%\" stop-color=\"#79b2ff\"/>"
        "<stop offset=\"60%\" stop-color=\"#0f5ea8\"/>"
        "<stop offset=\"100%\" stop-color=\"#11a9a0\"/>"
        "</linearGradient></defs>"
        "<circle cx=\"18\" cy=\"18\" r=\"16\" fill=\"url(#anLogoGrad)\"/>"
        "<path d=\"M13 22c1.6 1.9 4 3 6.6 3 2.1 0 4.1-.8 5.6-2.2-1.3.4-2.6.6-4 .4-4.3-.4-7.6-4.1-7.6-8.5 "
        "0-1.1.2-2.2.6-3.2-2.2 1.3-3.7 3.7-3.7 6.4 0 1.5.5 2.9 1.4 4.1z\" fill=\"#ffffff\" fill-opacity=\"0.93\"/>"
        "</svg>");
}

void appendSectionAnchor(QString& md, const QString& anchorId)
{
    md += QStringLiteral("<a id=\"%1\"></a>\n").arg(anchorId);
}

[[nodiscard]] QString formatWorkflowDuration(const qint64 ms)
{
    if (ms < 1000)
        return QStringLiteral("%1ms").arg(ms);
    qint64 totalSeconds = ms / 1000;
    if (totalSeconds < 60)
        return QStringLiteral("%1s").arg(totalSeconds);
    qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    if (minutes < 60)
        return QStringLiteral("%1m %2s").arg(minutes).arg(seconds);
    const qint64 hours = minutes / 60;
    minutes = minutes % 60;
    return QStringLiteral("%1h %2m %3s").arg(hours).arg(minutes).arg(seconds);
}

constexpr auto kExecutorTotalTimingToken = "{{AFTERNIGHT_EXECUTOR_TOTAL_MS}}";
constexpr auto kStageExecutionTimingToken = "{{AFTERNIGHT_STAGE_EXECUTION_MS}}";
constexpr auto kStateCommitTimingToken = "{{AFTERNIGHT_STATE_COMMIT_MS}}";
constexpr auto kArchiveTimingToken = "{{AFTERNIGHT_ARCHIVE_MS}}";
constexpr auto kReportTimingToken = "{{AFTERNIGHT_REPORT_MS}}";
constexpr auto kStartToReadyTimingToken = "{{AFTERNIGHT_START_TO_READY_MS}}";
constexpr auto kBytesHashedCounterToken = "{{AFTERNIGHT_BYTES_HASHED}}";
constexpr auto kBytesCopiedCounterToken = "{{AFTERNIGHT_BYTES_COPIED}}";
constexpr auto kSqliteStatementsCounterToken = "{{AFTERNIGHT_SQLITE_STATEMENTS}}";
constexpr auto kSqliteTransactionsStartedCounterToken = "{{AFTERNIGHT_SQLITE_TRANSACTIONS_STARTED}}";
constexpr auto kSqliteTransactionsCommittedCounterToken = "{{AFTERNIGHT_SQLITE_TRANSACTIONS_COMMITTED}}";
constexpr auto kSqliteTransactionsRolledBackCounterToken = "{{AFTERNIGHT_SQLITE_TRANSACTIONS_ROLLED_BACK}}";
constexpr auto kInputDecodeOperationsCounterToken = "{{AFTERNIGHT_INPUT_DECODE_OPERATIONS}}";
constexpr auto kScientificOutputEncodeCompletionsCounterToken = "{{AFTERNIGHT_SCIENTIFIC_OUTPUT_ENCODE_COMPLETIONS}}";
constexpr auto kInputPassesStartedCounterToken = "{{AFTERNIGHT_INPUT_PASSES_STARTED}}";
constexpr auto kTilePassesStartedCounterToken = "{{AFTERNIGHT_TILE_PASSES_STARTED}}";
constexpr auto kStackLoaderRequestsCounterToken = "{{AFTERNIGHT_STACK_LOADER_REQUESTS}}";
constexpr auto kRoiSubsetReadsCounterToken = "{{AFTERNIGHT_ROI_SUBSET_READS}}";
constexpr auto kFullFrameFallbackReadsCounterToken = "{{AFTERNIGHT_FULL_FRAME_FALLBACK_READS}}";
constexpr auto kLogicalDecodedPixelBytesCounterToken = "{{AFTERNIGHT_LOGICAL_DECODED_PIXEL_BYTES}}";
constexpr auto kCalibrationInvocationsCounterToken = "{{AFTERNIGHT_CALIBRATION_INVOCATIONS}}";
constexpr auto kCalibratedSpillFilesCreatedCounterToken = "{{AFTERNIGHT_CALIBRATED_SPILL_FILES_CREATED}}";
constexpr auto kCalibratedSpillBytesWrittenCounterToken = "{{AFTERNIGHT_CALIBRATED_SPILL_BYTES_WRITTEN}}";
constexpr auto kCalibratedSpillBytesReadCounterToken = "{{AFTERNIGHT_CALIBRATED_SPILL_BYTES_READ}}";
constexpr auto kCalibratedSpillRoiReadsCounterToken = "{{AFTERNIGHT_CALIBRATED_SPILL_ROI_READS}}";
constexpr auto kCalibratedSpillCleanupSuccessesCounterToken = "{{AFTERNIGHT_CALIBRATED_SPILL_CLEANUP_SUCCESSES}}";
constexpr auto kCalibratedSpillCleanupFailuresCounterToken = "{{AFTERNIGHT_CALIBRATED_SPILL_CLEANUP_FAILURES}}";
constexpr auto kDumpBytesReadCounterToken = "{{AFTERNIGHT_DUMP_BYTES_READ}}";
constexpr auto kDumpBytesWrittenCounterToken = "{{AFTERNIGHT_DUMP_BYTES_WRITTEN}}";
constexpr auto kPixmapBytesReadCounterToken = "{{AFTERNIGHT_PIXMAP_BYTES_READ}}";
constexpr auto kPixmapBytesWrittenCounterToken = "{{AFTERNIGHT_PIXMAP_BYTES_WRITTEN}}";

} // namespace
