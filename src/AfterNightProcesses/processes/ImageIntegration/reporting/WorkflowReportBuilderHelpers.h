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
 */
#pragma once

#include "ReportFormat.h"
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

[[nodiscard]] bool isAutoToken(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    return normalized.isEmpty() || normalized == QStringLiteral("auto");
}

[[nodiscard]] QString registrationBaseTransformDisplayName(const RegistrationSettings& settings)
{
    const QString trimmed = settings.transformation.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("Homography") : trimmed;
}

[[nodiscard]] QString transformKindDisplayName(const QString& kind)
{
    const QString normalized = kind.trimmed().toLower();
    if (normalized == QStringLiteral("similarity"))
    {
        return QStringLiteral("Similarity");
    }
    if (normalized == QStringLiteral("affine"))
    {
        return QStringLiteral("Affine");
    }
    if (normalized == QStringLiteral("homography"))
    {
        return QStringLiteral("Homography");
    }
    if (normalized == QStringLiteral("polynomial2"))
    {
        return QStringLiteral("Polynomial 2");
    }
    if (normalized == QStringLiteral("polynomial3"))
    {
        return QStringLiteral("Polynomial 3");
    }
    if (normalized == QStringLiteral("polynomial4"))
    {
        return QStringLiteral("Polynomial 4");
    }
    return kind.trimmed();
}

[[nodiscard]] bool isPolynomialTransformKind(const QString& kind)
{
    const QString normalized = kind.trimmed().toLower();
    return normalized == QStringLiteral("polynomial2") || normalized == QStringLiteral("polynomial3") ||
           normalized == QStringLiteral("polynomial4");
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

struct AutoTransformResolution
{
    QString detail;
    QString reason;
};

struct AutoDecisionRow
{
    QString area;
    QString group;
    QString requested;
    QString resolved;
    QString reason;
};

struct CometReportRun
{
    an::ui::CometTrackingRunRecord run;
    std::optional<an::ui::CometTrackingRunSummaryRecord> summary;
    std::vector<an::ui::CometTrackingPointRecord> points;
    std::optional<an::ui::CometTransformRoleProduct> cometAlignedProduct;
};

[[nodiscard]] AutoTransformResolution resolveAutoTransformDetail(const RegistrationSettings& settings,
                                                                 const QSet<QString>& metricKinds)
{
    const QString base = registrationBaseTransformDisplayName(settings);
    QStringList polynomialKinds;
    QStringList linearKinds;

    for (const QString& kind : metricKinds)
    {
        const QString display = transformKindDisplayName(kind);
        if (display.isEmpty())
        {
            continue;
        }
        if (isPolynomialTransformKind(kind))
        {
            polynomialKinds << display;
        }
        else
        {
            linearKinds << display;
        }
    }

    polynomialKinds.removeDuplicates();
    polynomialKinds.sort(Qt::CaseInsensitive);
    linearKinds.removeDuplicates();
    linearKinds.sort(Qt::CaseInsensitive);

    if (!polynomialKinds.isEmpty())
    {
        return {
            QStringLiteral("%1 + %2").arg(base, polynomialKinds.join(QStringLiteral(" / "))),
            QStringLiteral("Auto requested; metrics show %1 was used").arg(polynomialKinds.join(QStringLiteral(", ")))};
    }
    if (!linearKinds.isEmpty())
    {
        return {linearKinds.join(QStringLiteral(" / ")),
                QStringLiteral("Auto requested; metrics show %1 was used").arg(linearKinds.join(QStringLiteral(", ")))};
    }

    return {QStringLiteral("%1 + adaptive Polynomial 2-4").arg(base),
            QStringLiteral("Auto starts with %1 and tries Polynomial 2-4 only when the fit improves").arg(base)};
}

[[nodiscard]] QString registrationTransformDisplayName(const RegistrationSettings& settings,
                                                       const QString& resolvedAutoDetail = QString())
{
    const QString method =
        ImageIntegration::RegistrationSettingsUtils::normalizeRegistrationMethod(settings.registrationMethod);
    if (method == QStringLiteral("phase_correlation"))
    {
        return QStringLiteral("Phase Correlation");
    }
    if (method == QStringLiteral("manual_points"))
    {
        return QStringLiteral("Manual Points");
    }
    if (method == QStringLiteral("comet_tracking"))
    {
        return QStringLiteral("Comet Tracking");
    }

    const QString transform_mode = settings.transformMode.trimmed().toLower();
    if (transform_mode == QStringLiteral("auto"))
    {
        const QString detail =
            resolvedAutoDetail.trimmed().isEmpty()
                ? QStringLiteral("%1 + adaptive Polynomial 2-4").arg(registrationBaseTransformDisplayName(settings))
                : resolvedAutoDetail.trimmed();
        return QStringLiteral("Auto (%1)").arg(detail);
    }

    // The mode decides, as the star matcher reads it.
    const QString selection = ImageIntegration::RegistrationSettingsUtils::transformSelectionKey(
        settings.transformMode, settings.transformOrder, settings.transformation);
    if (selection.startsWith(QStringLiteral("polynomial")))
    {
        return QStringLiteral("Polynomial %1").arg(settings.transformOrder);
    }

    return registrationBaseTransformDisplayName(settings);
}

[[nodiscard]] QString interpolationDisplayText(const QString& interpolation)
{
    if (isAutoToken(interpolation))
    {
        return QStringLiteral("Auto (Lanczos4)");
    }
    return interpolation.trimmed().isEmpty() ? QStringLiteral("Lanczos4") : interpolation.trimmed();
}

[[nodiscard]] QString cfaPatternDisplayText(const QString& cfaPattern)
{
    if (isAutoToken(cfaPattern))
    {
        return QStringLiteral("Auto (from metadata)");
    }
    return cfaPattern.trimmed().isEmpty() ? QStringLiteral("Auto (from metadata)") : cfaPattern.trimmed();
}

[[nodiscard]] QString debayerMethodDisplayText(const QString& debayerMethod)
{
    if (isAutoToken(debayerMethod))
    {
        return QStringLiteral("Auto (engine-selected)");
    }
    return debayerMethod.trimmed().isEmpty() ? QStringLiteral("Auto (engine-selected)") : debayerMethod.trimmed();
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

[[nodiscard]] QString boolText(bool value)
{
    return value ? QStringLiteral("Yes") : QStringLiteral("No");
}

[[nodiscard]] QString normalizationModeText(NormalizationMode mode)
{
    switch (mode)
    {
    case NormalizationMode::None:
        return QStringLiteral("None");
    case NormalizationMode::Global:
        return QStringLiteral("Global");
    case NormalizationMode::Local:
        return QStringLiteral("Local");
    }

    return QStringLiteral("Unknown");
}

[[nodiscard]] QString globalNormalizationMethodText(GlobalNormalizationMethod method)
{
    switch (method)
    {
    case GlobalNormalizationMethod::Additive:
        return QStringLiteral("Additive");
    case GlobalNormalizationMethod::AdditiveScaling:
        return QStringLiteral("Additive + Scaling");
    case GlobalNormalizationMethod::Multiplicative:
        return QStringLiteral("Multiplicative");
    case GlobalNormalizationMethod::MultiplicativeScaling:
        return QStringLiteral("Multiplicative + Scaling");
    }

    return QStringLiteral("Unknown");
}

[[nodiscard]] QString scaleEstimatorText(ScaleEstimatorMethod method)
{
    switch (method)
    {
    case ScaleEstimatorMethod::MAD:
        return QStringLiteral("MAD");
    case ScaleEstimatorMethod::IKSS:
        return QStringLiteral("IKSS");
    }

    return QStringLiteral("Unknown");
}

[[nodiscard]] QString rejectionMethodText(const StackingSettings& settings)
{
    const QString effective =
        settings.rejectionMethod.trimmed().isEmpty() ? QStringLiteral("None") : settings.rejectionMethod.trimmed();
    const QString requested = settings.requestedRejectionMethod.trimmed();
    if (!requested.isEmpty() && requested.compare(effective, Qt::CaseInsensitive) != 0)
    {
        if (requested.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0)
        {
            return QStringLiteral("Auto (%1)").arg(effective);
        }
        return QStringLiteral("%1 -> %2").arg(requested, effective);
    }
    return effective;
}

[[nodiscard]] QString frameLabel(const FrameId& id)
{
    const QString canonical = id.canonicalPath().trimmed();
    if (canonical.isEmpty())
    {
        return QStringLiteral("-");
    }

    return QFileInfo(canonical).fileName();
}

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

/// Extract the observing date (YYYY-MM-DD) from a LIGHT file path.
/// Looks for a date-like directory component or filename prefix.
[[nodiscard]] QDate extractObservingDate(const QString& filePath)
{
    // Try to match YYYY-MM-DD in the path
    static const QRegularExpression dateRx(QStringLiteral("(\\d{4}-\\d{2}-\\d{2})"));
    auto match = dateRx.match(filePath);
    if (match.hasMatch())
    {
        const QDate d = QDate::fromString(match.captured(1), QStringLiteral("yyyy-MM-dd"));
        if (d.isValid())
            return d;
    }
    return {};
}

/// Extract sensor temperature from a filename like ..._-10.00_60.00s_0003.fits
[[nodiscard]] std::optional<double> extractSensorTemperature(const QString& filePath)
{
    static const QRegularExpression tempRx(QStringLiteral("_(-?\\d+\\.\\d+)_\\d+\\.\\d+s_"));
    auto match = tempRx.match(filePath);
    if (match.hasMatch())
    {
        bool ok = false;
        const double temp = match.captured(1).toDouble(&ok);
        if (ok)
            return temp;
    }
    return std::nullopt;
}

/// Extract sub-exposure duration (seconds) from a filename like ..._-10.00_60.00s_0003.fits
[[nodiscard]] std::optional<double> extractExposureFromFilename(const QString& filePath)
{
    static const QRegularExpression expRx(QStringLiteral("_(\\d+(?:\\.\\d+)?)s(?=_|\\.|$)"),
                                          QRegularExpression::CaseInsensitiveOption);
    auto match = expRx.match(filePath);
    if (match.hasMatch())
    {
        bool ok = false;
        const double sec = match.captured(1).toDouble(&ok);
        if (ok && sec > 0)
            return sec;
    }
    return std::nullopt;
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

struct ReportExposureBucket
{
    double exposureSeconds = 0.0;
    int frameCount = 0;
};

struct ExposureBreakdown
{
    int totalFrames = 0;
    int knownFrames = 0;
    int unknownFrames = 0;
    int totalMs = 0;
    QList<ReportExposureBucket> buckets;
};

[[nodiscard]] std::optional<double> parseGroupExposure(const WorkflowManifest::IntegrationGroup& group)
{
    bool ok = false;
    const double fromField = group.exposure.toDouble(&ok);
    if (ok && fromField > 0.0)
    {
        return fromField;
    }
    return std::nullopt;
}

void addExposureBucket(QList<ReportExposureBucket>& buckets, double exposureSeconds, int frameCount)
{
    constexpr double kExposureMergeToleranceSeconds = 1e-3;
    for (auto& bucket : buckets)
    {
        if (std::abs(bucket.exposureSeconds - exposureSeconds) <= kExposureMergeToleranceSeconds)
        {
            bucket.frameCount += frameCount;
            return;
        }
    }

    buckets.append(ReportExposureBucket{exposureSeconds, frameCount});
}

[[nodiscard]] ExposureBreakdown buildExposureBreakdown(const WorkflowManifest::IntegrationGroup& group)
{
    ExposureBreakdown breakdown;
    breakdown.totalFrames = static_cast<int>(group.lightFiles.size());

    const std::optional<double> groupExposure = parseGroupExposure(group);
    const bool hasEmbeddedExposures = group.lightExposureSeconds.size() == group.lightFiles.size();
    for (size_t i = 0; i < group.lightFiles.size(); ++i)
    {
        const auto& lightFile = group.lightFiles[i];

        std::optional<double> resolvedExposure;
        if (hasEmbeddedExposures)
        {
            const double embeddedExposure = group.lightExposureSeconds[i];
            if (embeddedExposure > 0.0)
            {
                resolvedExposure = embeddedExposure;
            }
        }

        if (!resolvedExposure.has_value())
        {
            resolvedExposure = extractExposureFromFilename(lightFile.canonicalPath());
        }
        if (!resolvedExposure.has_value() && groupExposure.has_value())
        {
            resolvedExposure = groupExposure;
        }

        if (resolvedExposure.has_value() && *resolvedExposure > 0.0)
        {
            addExposureBucket(breakdown.buckets, *resolvedExposure, 1);
            ++breakdown.knownFrames;
            breakdown.totalMs += static_cast<int>(std::llround(*resolvedExposure * 1000.0));
        }
        else
        {
            ++breakdown.unknownFrames;
        }
    }

    std::sort(breakdown.buckets.begin(), breakdown.buckets.end(),
              [](const ReportExposureBucket& lhs, const ReportExposureBucket& rhs) {
                  return lhs.exposureSeconds < rhs.exposureSeconds;
              });

    return breakdown;
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

struct GroupOutcomeCounts
{
    int totalGroups = 0;
    int regeneratedGroups = 0;
    int reusedGroups = 0;
    int failedGroups = 0;
};

[[nodiscard]] GroupOutcomeCounts computeGroupOutcomeCounts(const WorkflowManifest& manifest,
                                                           const WorkflowExecutor::Result& result)
{
    GroupOutcomeCounts counts;
    counts.totalGroups = static_cast<int>(manifest.integrationGroups.size());

    for (const auto& groupResult : result.integrationGroupResults)
    {
        if (groupResult.success)
        {
            ++counts.regeneratedGroups;
        }
        else
        {
            ++counts.failedGroups;
        }
    }

    if (counts.totalGroups <= 0)
    {
        counts.totalGroups = counts.regeneratedGroups + counts.failedGroups;
    }

    const int reused = counts.totalGroups - counts.regeneratedGroups - counts.failedGroups;
    counts.reusedGroups = reused > 0 ? reused : 0;
    return counts;
}

[[nodiscard]] QString stageDisplayName(QString stageName)
{
    for (int i = stageName.length() - 1; i > 0; --i)
    {
        if (stageName[i].isUpper() && (stageName[i - 1].isLower() || stageName[i - 1].isDigit()))
        {
            stageName.insert(i, QLatin1Char(' '));
        }
    }
    return stageName.trimmed();
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

struct InputFileSets
{
    QSet<QString> biasFiles;
    QSet<QString> darkFiles;
    QSet<QString> flatFiles;
    QSet<QString> lightFiles;

    [[nodiscard]] bool isEmpty() const
    {
        return lightFiles.isEmpty() && biasFiles.isEmpty() && darkFiles.isEmpty() && flatFiles.isEmpty();
    }
};

[[nodiscard]] InputFileSets collectInputFileSets(const WorkflowManifest& manifest)
{
    InputFileSets sets;

    if (manifest.biasStage.has_value())
    {
        for (const auto& group : manifest.biasStage->groups)
        {
            for (const auto& file : group.inputFiles)
            {
                sets.biasFiles.insert(file.canonicalPath());
            }
        }
    }
    if (manifest.darkStage.has_value())
    {
        for (const auto& group : manifest.darkStage->groups)
        {
            for (const auto& file : group.inputFiles)
            {
                sets.darkFiles.insert(file.canonicalPath());
            }
        }
    }
    if (manifest.flatStage.has_value())
    {
        for (const auto& group : manifest.flatStage->groups)
        {
            for (const auto& file : group.inputFiles)
            {
                sets.flatFiles.insert(file.canonicalPath());
            }
        }
    }
    for (const auto& group : manifest.integrationGroups)
    {
        for (const auto& file : group.lightFiles)
        {
            sets.lightFiles.insert(file.canonicalPath());
        }
    }

    return sets;
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

[[nodiscard]] QString sha256Hex(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
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

struct RegistrationMetricRow
{
    int stars = 0;
    double fwhm = 0.0;
    double snr = 0.0;
    double eccentricity = 0.0;
    double photometricZeroPoint = 0.0;
    double zeroPointScatter = 0.0;
    double astrometricRmsPixels = 0.0;
    double astrometricRmsArcsec = 0.0;
    double limitingMagnitude = 0.0;
    int catalogMatchedStars = 0;
    bool succeeded = false;
    QString transformKind;
};

struct QualityWeightSample
{
    QString frameId;
    double exposureSeconds = 0.0;
    FrameQualityMetrics metrics;
    /// The background noise and scales the registration recorded, read for a group weighted by noise.
    FrameNoiseStatistics noise;
};

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

struct SpccHandoffGroupSummary
{
    QString groupName;
    int settingsVersion = -1;
    int frameCount = 0;
    int referenceCount = 0;
    int sourceCount = 0;
    int satisfiedCount = 0;
    int missingSourceCount = 0;
    int missingChunkOrCacheCount = 0;
    int manifestMismatchCount = 0;
    int staleMatchCount = 0;
    int rejectedQualityFieldsCount = 0;
    int untestedCount = 0;
    QString astrometricManifestId;
    QString spccManifestId;
    QList<double> matchSeparationsArcsec;
    QList<double> residualsPx;
};

/// Whether the report shows a weighting's weights against the group's floor: the star-count and width weights, which
/// the stack raises to the floor on the scale the report shows them, relative to the heaviest frame. A noise or image
/// count weight takes no floor, and the photometric model's floor is on its raw scale, not the one it is shown on.
[[nodiscard]] bool showsWeightFloor(const QString& weighting)
{
    const ImageIntegration::PersistedChoiceValue value =
        ImageIntegration::persistedChoiceFromText(ImageIntegration::PersistedChoice::Weighting, weighting);
    const std::optional<an::StackWeighting> source =
        value.known ? ImageIntegration::stackWeightingForKey(value.key) : std::nullopt;
    return source.has_value() && an::stackWeightingUsesMinimumWeight(*source);
}

[[nodiscard]] std::optional<double> medianOf(QList<double> values)
{
    if (values.isEmpty())
    {
        return std::nullopt;
    }

    std::sort(values.begin(), values.end());
    const int middle = values.size() / 2;
    if ((values.size() % 2) == 0)
    {
        return (values[middle - 1] + values[middle]) * 0.5;
    }
    return values[middle];
}

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

[[nodiscard]] std::optional<int>
queryRegistrationSettingsVersion(QSqlDatabase& db, const QString& groupKey,
                                 WorkflowExecutionCounterAccumulator* counters = nullptr)
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
                               WorkflowExecutionCounterAccumulator* counters = nullptr)
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
                             WorkflowExecutionCounterAccumulator* counters = nullptr)
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

[[nodiscard]] QSet<QString> successfulTransformKinds(const QHash<QString, RegistrationMetricRow>& metricsByFrame)
{
    QSet<QString> kinds;
    for (auto it = metricsByFrame.cbegin(); it != metricsByFrame.cend(); ++it)
    {
        const RegistrationMetricRow& row = it.value();
        if (row.succeeded && !row.transformKind.trimmed().isEmpty())
        {
            kinds.insert(row.transformKind.trimmed().toLower());
        }
    }
    return kinds;
}

[[nodiscard]] AutoTransformResolution
resolveAutoTransformDetail(const WorkflowManifest::RegistrationGroup& registrationGroup, const QString& metricsDbPath,
                           WorkflowExecutionCounterAccumulator* counters)
{
    return resolveAutoTransformDetail(registrationGroup.settings,
                                      successfulTransformKinds(loadRegistrationMetricsForGroup(
                                          metricsDbPath, registrationGroup.groupKey.toString(), counters)));
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

[[nodiscard]] double resolveLightExposureSeconds(const WorkflowManifest::IntegrationGroup& group, int index)
{
    if (index >= 0 && index < static_cast<int>(group.lightExposureSeconds.size()))
    {
        const double embeddedExposure = group.lightExposureSeconds[static_cast<size_t>(index)];
        if (embeddedExposure > 0.0 && std::isfinite(embeddedExposure))
        {
            return embeddedExposure;
        }
    }

    if (index >= 0 && index < static_cast<int>(group.lightFiles.size()))
    {
        const auto fromName = extractExposureFromFilename(group.lightFiles[static_cast<size_t>(index)].canonicalPath());
        if (fromName.has_value() && *fromName > 0.0)
        {
            return *fromName;
        }
    }

    const auto groupExposure = parseGroupExposure(group);
    if (groupExposure.has_value() && *groupExposure > 0.0)
    {
        return *groupExposure;
    }

    return 0.0;
}

/// What the report reads of a file's header: its size and its metadata, never its pixels.
struct ReportHeader
{
    /// The image's width in pixels.
    std::uint64_t width = 0;

    /// The image's height in pixels.
    std::uint64_t height = 0;

    /// The metadata the header states.
    an::ImageMetadata metadata{};
};

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

/// The weight the stack gave each sample: the engine's own weighting over the measurements the stacking stage builds
/// from the registration's records (`deriveStackWeights`), under the group's settings as its stack reads them and
/// against the group's reference, derived in the session the builder is given. The photometric weights are shown at the
/// engine's mean of one, every other weighting's relative to the heaviest frame - the scale the group's floor is stated
/// on. Every frame weighs one where the stack weighs them equally - no weighting, a figure some frame lacks - and
/// without a session.
///
/// @param session the session the weights are derived and the headers read in, or null.
/// @param samples the group's measured frames.
/// @param group the group.
/// @return one weight per sample.
[[nodiscard]] QList<double> computeWeightsForSamples(const an::consumer::EngineSession* session,
                                                     const QList<QualityWeightSample>& samples,
                                                     const WorkflowManifest::IntegrationGroup& group)
{
    QList<double> weights(samples.size(), 1.0);
    const StackingSettings sanitized = ImageIntegration::sanitizeStackingSettings(group.stacking);
    const an::Result<ImageIntegration::IntegrationStackChoice> choice =
        ImageIntegration::integrationStackChoice(sanitized);
    if (session == nullptr || samples.isEmpty() || !choice.hasValue() ||
        choice->weighting == an::StackWeighting::Uniform)
    {
        return weights;
    }

    const an::StackWeighting source = choice->weighting;
    // One operation reads every header an image count needs and derives the weights.
    an::Result<an::consumer::OperationScope> scope = session->admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .maxConcurrency = 1u});
    if (!scope.hasValue())
    {
        return weights;
    }
    std::vector<FrameId> frames;
    std::vector<an::StackFrameMeasurement> measurements;
    frames.reserve(static_cast<std::size_t>(samples.size()));
    measurements.reserve(static_cast<std::size_t>(samples.size()));
    for (const auto& sample : samples)
    {
        frames.emplace_back(sample.frameId);
        an::StackFrameMeasurement measurement =
            ImageIntegration::stackFrameMeasurementFor(sample.metrics, sample.noise);
        if (source == an::StackWeighting::ImageCount)
        {
            // How many frames the light's header says it combines, as the stack reads it; one where it cannot be read.
            const std::string utf8Path = sample.frameId.toUtf8().toStdString();
            const an::Result<an::CodecImageInfo> probed = an::probeImage(
                scope->engine(), scope->execution(), an::CodecReadRequest{.path = utf8Path, .validatePayload = false});
            measurement.integratedFrameCount =
                probed.hasValue() ? ImageIntegration::combinedFrameCount(probed->metadata.codec().cards()) : 1u;
        }
        else if (!ImageIntegration::carriesWeightingFigure(source, measurement))
        {
            // The stack weighs a set by a figure only when every frame carries it.
            return weights;
        }
        measurements.push_back(std::move(measurement));
    }
    const std::optional<FrameId> reference = group.normalizationReferenceFrames.empty()
                                                 ? std::nullopt
                                                 : std::optional<FrameId>(group.normalizationReferenceFrames.front());
    const an::Result<ImageIntegration::DerivedStackWeights> derived = ImageIntegration::deriveStackWeights(
        scope->execution(), ImageIntegration::stackWeightingRequestFor(sanitized, source, choice->starCountScaling),
        measurements, ImageIntegration::stackWeightingReferenceIndex(frames, reference));
    if (!derived.hasValue())
    {
        LOG_WARNING("WorkflowReportBuilder: frame weights not derived: {}", derived.status().message());
        return weights;
    }
    const double heaviest = *std::max_element(derived->weights.begin(), derived->weights.end());
    const bool relative = source != an::StackWeighting::Photometric && heaviest > 0.0;
    for (qsizetype i = 0; i < weights.size(); ++i)
    {
        const double weight = derived->weights[static_cast<std::size_t>(i)];
        weights[i] = relative ? weight / heaviest : weight;
    }
    return weights;
}

/// The weight the stack recorded combining each sample with, on the scale `computeWeightsForSamples` shows: relative
/// to the heaviest light, the photometric model's at its mean of one. A light the master does not combine - one a
/// spatial re-stack left out - weighs nothing, and the others are as the stack weighed them among the lights it kept.
///
/// @param samples the group's measured frames.
/// @param quality the record of the group's stack, which holds its lights' weights.
/// @param group the group.
/// @return one weight per sample.
[[nodiscard]] QList<double> recordedWeightsForSamples(const QList<QualityWeightSample>& samples,
                                                      const StackQualityRecord& quality,
                                                      const WorkflowManifest::IntegrationGroup& group)
{
    QHash<QString, double> byLight;
    byLight.reserve(static_cast<qsizetype>(quality.lightWeights.size()));
    double heaviest = 0.0;
    for (const StackLightWeight& entry : quality.lightWeights)
    {
        byLight.insert(FrameId(entry.light).identityKey(), entry.weight);
        heaviest = (std::max)(heaviest, entry.weight);
    }
    const an::Result<ImageIntegration::IntegrationStackChoice> choice =
        ImageIntegration::integrationStackChoice(ImageIntegration::sanitizeStackingSettings(group.stacking));
    const bool relative =
        !(choice.hasValue() && choice->weighting == an::StackWeighting::Photometric) && heaviest > 0.0;
    QList<double> weights;
    weights.reserve(samples.size());
    for (const QualityWeightSample& sample : samples)
    {
        const double weight = byLight.value(FrameId(sample.frameId).identityKey(), 0.0);
        weights.append(relative ? weight / heaviest : weight);
    }
    return weights;
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

[[nodiscard]] QString resolveTargetName(const WorkflowManifest::IntegrationGroup& group)
{
    const QString directObject = group.object.trimmed();
    if (!directObject.isEmpty())
    {
        return directObject;
    }

    GroupingOptions options;
    options.byObject = true;
    options.bySession = true;

    QString parsedObject;
    GroupKey::parseMetadata(group.groupKey.key, FrameType::Light, options, &parsedObject, nullptr);
    return parsedObject.trimmed();
}

[[nodiscard]] QString canonicalOrRawPath(const FrameId& id)
{
    const QString canonical = id.canonicalPath();
    return canonical.isEmpty() ? id.toString() : canonical;
}

struct RunDeltaMetrics
{
    int totalLightFrames = 0;
    int totalIntegrationMs = 0;
    qint64 totalOutputBytes = 0;
    std::optional<double> avgSnrProxy;
    std::optional<double> avgOverlapRetentionPct;
};

struct ArchivedRunSnapshot
{
    QString executionDirName;
    QString manifestPath;
    QString archivedAtLocal;
    WorkflowManifest manifest;
    std::vector<IntegrationGroupResult> integrationGroupResults;
};

[[nodiscard]] std::optional<QPair<int, int>> parseResolutionDimensions(const QString& resolution);

template <typename ValueFn>
[[nodiscard]] std::optional<double> averageChannelStat(const std::vector<RawChannelStats>& stats, ValueFn&& valueFn);

[[nodiscard]] QString resolvePathAgainstBase(const QString& candidatePath, const QString& baseDir)
{
    const QString trimmed = candidatePath.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    const QFileInfo info(trimmed);
    if (info.isAbsolute() || baseDir.trimmed().isEmpty())
    {
        return trimmed;
    }

    return QDir(baseDir).filePath(trimmed);
}

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

[[nodiscard]] std::optional<QPair<int, int>> parseResolutionDimensions(const QString& resolution)
{
    const QString trimmed = resolution.trimmed();
    if (trimmed.isEmpty())
    {
        return std::nullopt;
    }

    static const QRegularExpression resRx(QStringLiteral("^(\\d+)\\s*[xX]\\s*(\\d+)$"));
    const auto match = resRx.match(trimmed);
    if (!match.hasMatch())
    {
        return std::nullopt;
    }

    bool okW = false;
    bool okH = false;
    const int width = match.captured(1).toInt(&okW);
    const int height = match.captured(2).toInt(&okH);
    if (!okW || !okH || width <= 0 || height <= 0)
    {
        return std::nullopt;
    }

    return QPair<int, int>(width, height);
}

template <typename ValueFn>
[[nodiscard]] std::optional<double> averageChannelStat(const std::vector<RawChannelStats>& stats, ValueFn&& valueFn)
{
    if (stats.empty())
    {
        return std::nullopt;
    }

    double sum = 0.0;
    int count = 0;
    for (const auto& channel : stats)
    {
        const double v = valueFn(channel);
        if (!std::isfinite(v))
        {
            continue;
        }
        sum += v;
        ++count;
    }

    if (count <= 0)
    {
        return std::nullopt;
    }
    return sum / static_cast<double>(count);
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

[[nodiscard]] QStringList collectTargets(const WorkflowManifest& manifest)
{
    QSet<QString> targets;
    GroupingOptions options;
    options.byObject = true;
    options.bySession = true;

    for (const auto& group : manifest.integrationGroups)
    {
        const QString directObject = group.object.trimmed();
        if (!directObject.isEmpty())
        {
            targets.insert(directObject);
            continue;
        }

        QString parsedObject;
        GroupKey::parseMetadata(group.groupKey.key, FrameType::Light, options, &parsedObject, nullptr);
        if (!parsedObject.trimmed().isEmpty())
        {
            targets.insert(parsedObject.trimmed());
        }
    }

    QStringList list = targets.values();
    std::sort(list.begin(), list.end(),
              [](const QString& lhs, const QString& rhs) { return lhs.compare(rhs, Qt::CaseInsensitive) < 0; });
    return list;
}

/// A header text as the report shows it: trimmed, and nothing for the dash a header writes for a figure it does not
/// have.
[[nodiscard]] QString normalizeMetadataText(const QString& rawText)
{
    const QString text = rawText.trimmed();
    if (text.isEmpty() || text == QStringLiteral("-"))
    {
        return {};
    }

    return text;
}

[[nodiscard]] QString normalizeMetadataText(const std::string& rawText)
{
    return normalizeMetadataText(QString::fromStdString(rawText));
}

/// A header card's value without the quotes a FITS string is written in.
[[nodiscard]] QString unquotedCardText(const std::string& value)
{
    QString text = QString::fromStdString(value).trimmed();
    if (text.size() >= 2 && text.startsWith(QLatin1Char('\'')) && text.endsWith(QLatin1Char('\'')))
    {
        text = text.mid(1, text.size() - 2).replace(QStringLiteral("''"), QStringLiteral("'"));
    }
    return text.trimmed();
}

[[nodiscard]] QString readFirstFitsValue(const an::ImageMetadata& metadata, const QStringList& keys)
{
    const std::span<const an::CodecCard> cards = metadata.codec().cards();
    for (const auto& key : keys)
    {
        for (const an::CodecCard& card : cards)
        {
            if (card.origin() != an::CodecNamespace::Fits ||
                QString::fromStdString(card.keyword()).compare(key, Qt::CaseInsensitive) != 0)
            {
                continue;
            }
            const QString value = normalizeMetadataText(unquotedCardText(card.value()));
            if (!value.isEmpty())
            {
                return value;
            }
        }
    }

    return {};
}

/// The target a header names: the acquisition record's object, or the one its cards state.
[[nodiscard]] QString metadataObjectName(const an::ImageMetadata& metadata)
{
    const QString stated = normalizeMetadataText(metadata.acquisition().objectName());
    if (!stated.isEmpty())
    {
        return stated;
    }
    return normalizeMetadataText(an::projectAcquisition(metadata.codec()).acquisition.objectName);
}

/// The camera a header names: the acquisition record's instrument, or the one its cards state.
[[nodiscard]] QString metadataCameraModel(const an::ImageMetadata& metadata)
{
    const QString stated = normalizeMetadataText(metadata.acquisition().instrumentName());
    if (!stated.isEmpty())
    {
        return stated;
    }
    return normalizeMetadataText(an::projectAcquisition(metadata.codec()).acquisition.instrumentName);
}

/// Whether a header states an exposure, in its acquisition record or its cards.
[[nodiscard]] bool metadataStatesExposure(const an::ImageMetadata& metadata)
{
    return metadata.acquisition().exposureSeconds().has_value() ||
           an::projectAcquisition(metadata.codec()).acquisition.exposureSeconds.has_value();
}

/// The telescope a header names: its `TELESCOP` card, or the acquisition record's telescope.
[[nodiscard]] QString metadataTelescope(const an::ImageMetadata& metadata)
{
    const QString carded = readFirstFitsValue(metadata, {QStringLiteral("TELESCOP"), QStringLiteral("TELESCOPE")});
    if (!carded.isEmpty())
    {
        return carded;
    }
    const QString stated = normalizeMetadataText(metadata.acquisition().telescopeName());
    if (!stated.isEmpty())
    {
        return stated;
    }
    return normalizeMetadataText(an::projectAcquisition(metadata.codec()).acquisition.telescopeName);
}

/// The ISO speed a camera reported, or else its gain setting, as the image summary reads them.
[[nodiscard]] QString formatIsoGainLabel(const an::ImageMetadata& metadata)
{
    const an::AcquisitionMetadata& acquisition = metadata.acquisition();
    const an::AcquisitionProjection projected = an::projectAcquisition(metadata.codec());

    std::optional<std::int64_t> iso = projected.isoSpeed;
    if (!iso.has_value() && acquisition.rawCamera().has_value() && acquisition.rawCamera()->isoSpeed > 0.0)
    {
        iso = std::llround(acquisition.rawCamera()->isoSpeed);
    }
    if (iso.has_value())
    {
        return QStringLiteral("ISO %1").arg(static_cast<qlonglong>(*iso));
    }
    if (projected.gainSetting.has_value())
    {
        return QStringLiteral("Gain %1").arg(static_cast<qlonglong>(*projected.gainSetting));
    }
    return {};
}

[[nodiscard]] QString formatOffsetLabel(const an::ImageMetadata& metadata)
{
    const an::AcquisitionProjection projected = an::projectAcquisition(metadata.codec());
    if (!projected.cameraOffset.has_value())
    {
        return {};
    }

    return QStringLiteral("Offset %1").arg(static_cast<qlonglong>(*projected.cameraOffset));
}

[[nodiscard]] QString formatLocationFromFits(const an::ImageMetadata& metadata)
{
    const QString lat = readFirstFitsValue(metadata, {QStringLiteral("SITELAT"), QStringLiteral("OBS-LAT"),
                                                      QStringLiteral("LAT-OBS"), QStringLiteral("OBSGEO-B")});
    const QString lon = readFirstFitsValue(metadata, {QStringLiteral("SITELONG"), QStringLiteral("OBS-LONG"),
                                                      QStringLiteral("LONG-OBS"), QStringLiteral("OBSGEO-L")});

    if (!lat.isEmpty() && !lon.isEmpty())
    {
        return QStringLiteral("%1, %2").arg(lat, lon);
    }

    return {};
}

[[nodiscard]] QString resolveAppVersion(const QJsonObject& context)
{
    const QString contextVersion = context.value(QStringLiteral("appVersion")).toString().trimmed();
    if (!contextVersion.isEmpty())
    {
        return contextVersion;
    }

    const QString engineVersion = QString::fromUtf8(an::engineVersionString()).trimmed();
    if (!engineVersion.isEmpty())
    {
        return engineVersion;
    }

    return QStringLiteral("Unknown");
}

[[nodiscard]] QString resolveContextValue(const QJsonObject& context, const QString& key,
                                          const QString& fallback = QString())
{
    const QString value = context.value(key).toString().trimmed();
    return value.isEmpty() ? fallback : value;
}

[[nodiscard]] QString reportProcessLabel(const WorkflowManifest& manifest)
{
    const QString label = manifest.processLabel.trimmed();
    return label.isEmpty() ? QStringLiteral("Image Integration") : label;
}

[[nodiscard]] QString reportTitle(const WorkflowManifest& manifest)
{
    return manifest.processLabel.trimmed().isEmpty()
               ? QStringLiteral("AfterNight Integration Report")
               : QStringLiteral("AfterNight %1 Report").arg(manifest.processLabel.trimmed());
}

[[nodiscard]] QString reportProcessingProfileText(const WorkflowManifest& manifest)
{
    const QString label = manifest.processingProfileLabel.trimmed();
    const QString id = manifest.processingProfileId.trimmed();
    if (label.isEmpty())
    {
        return id;
    }
    if (id.isEmpty())
    {
        return label;
    }
    return QStringLiteral("%1 (`%2`)").arg(label, id);
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
