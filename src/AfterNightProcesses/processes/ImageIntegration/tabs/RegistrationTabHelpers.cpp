// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "RegistrationTab.h"
#include "RegistrationTabUiBuilder.h"
#include "TabStyling.h"

#include "controls/IAppContext.h"
#include "processes/ImageIntegration/controls/RegistrationSettingsDialog.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/model/FilterStatistics.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ImageIntegration/core/BlinkComparatorExclusionUtils.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"
#include "processes/ImageIntegration/dialogs/CometTrackingDialogAutoTrackAdapter.h"
#include "processes/ImageIntegration/workflow/RegistrationWorkAnalyzer.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionController.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsSchema.h"
#include "processes/core/RegistrationMetricsWriter.h"
#include "RegistrationMetricsDataAccess.h"

#include <AfterNightPlatform/Logging.h>

#include <QBoxLayout>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QHash>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFutureWatcher>
#include <QFrame>
#include <QLabel>
#include <QPalette>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVector>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QShortcut>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QStringList>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QUndoStack>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <unordered_map>

#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "processes/ImageIntegration/widgets/QualityDistributionWidget.h"
#include "theme/AnDesignTokens.h"

#include "RegistrationTabImpl.h"

namespace ImageIntegration::detail
{

[[nodiscard]] bool frameFailsQualityThresholds(const FrameQualityMetrics& metrics, const double score,
                                               const QualityThresholdConfig& thresholds)
{
    if (thresholds.minScoreEnabled && score < thresholds.minScore)
    {
        return true;
    }
    if (thresholds.minStarsEnabled && metrics.stars < thresholds.minStars)
    {
        return true;
    }
    if (thresholds.minSnrEnabled && metrics.snr < thresholds.minSnr)
    {
        return true;
    }
    if (thresholds.maxFwhmEnabled && metrics.fwhm > thresholds.maxFwhm)
    {
        return true;
    }
    if (thresholds.maxEccEnabled && metrics.eccentricity > thresholds.maxEcc)
    {
        return true;
    }
    return false;
}

QString translatedRegistrationWorkReason(const RegistrationWorkReason& reason)
{
    switch (reason.code)
    {
    case RegistrationWorkReasonCode::NoPreviousState:
        return RegistrationTab::tr("No previous registration run found for this group.");
    case RegistrationWorkReasonCode::DetectionSettingsChanged:
        return RegistrationTab::tr("Star detection settings changed.");
    case RegistrationWorkReasonCode::RegistrationSettingsChanged:
        return RegistrationTab::tr("Registration settings changed.");
    case RegistrationWorkReasonCode::CalibrationChanged:
        return RegistrationTab::tr("Calibration assignments changed.");
    case RegistrationWorkReasonCode::ReferenceChanged:
        return RegistrationTab::tr("Reference frame changed.");
    case RegistrationWorkReasonCode::FrameListChanged:
        return RegistrationTab::tr("Group frame list changed.");
    case RegistrationWorkReasonCode::SettingsVersionChanged:
        return RegistrationTab::tr("Registration settings version changed.");
    case RegistrationWorkReasonCode::StarCacheUnavailable:
        return RegistrationTab::tr("Star detection cache is missing or unavailable.");
    case RegistrationWorkReasonCode::MissingCachedStars:
        return RegistrationTab::tr("%1 frame(s) missing cached stars.").arg(reason.count);
    case RegistrationWorkReasonCode::ChangedInputs:
        return RegistrationTab::tr("%1 frame(s) changed on disk since star detection.").arg(reason.count);
    case RegistrationWorkReasonCode::MissingMetrics:
        return RegistrationTab::tr("%1 frame(s) missing metrics.").arg(reason.count);
    case RegistrationWorkReasonCode::FailedMetrics:
        return RegistrationTab::tr("%1 frame(s) failed registration.").arg(reason.count);
    }

    return RegistrationTab::tr("Registration data is out of date.");
}

QString registrationTransformDisplayName(const RegistrationSettings& settings)
{
    const QString method =
        ImageIntegration::RegistrationSettingsUtils::normalizeRegistrationMethod(settings.registrationMethod);
    if (method == QStringLiteral("phase_correlation"))
    {
        return RegistrationTab::tr("Phase Correlation");
    }
    if (method == QStringLiteral("manual_points"))
    {
        return RegistrationTab::tr("Manual Points");
    }
    if (method == QStringLiteral("comet_tracking"))
    {
        return RegistrationTab::tr("Comet Tracking");
    }

    const QString transform_mode = settings.transformMode.trimmed().toLower();
    if (transform_mode == QStringLiteral("auto"))
    {
        return RegistrationTab::tr("Auto");
    }

    // The mode decides, as the star matcher reads it.
    const QString selection = ImageIntegration::RegistrationSettingsUtils::transformSelectionKey(
        settings.transformMode, settings.transformOrder, settings.transformation);
    if (selection.startsWith(QStringLiteral("polynomial")))
    {
        return RegistrationTab::tr("Polynomial %1").arg(settings.transformOrder);
    }

    return settings.transformation.trimmed().isEmpty() ? RegistrationTab::tr("Homography") : settings.transformation;
}

void applyStatusTone(QLabel* label, const char* tone)
{
    if (!label)
    {
        return;
    }

    label->setProperty("anStatusTone", QString::fromLatin1(tone));
    if (QStyle* style = label->style())
    {
        style->unpolish(label);
        style->polish(label);
    }
    label->update();
}

QString buildFilterNormalizedMetricTooltip(const QString& metricName, double absoluteValue, const QString& filterKey,
                                           const QHash<QString, FilterStatistics>& filterStats, bool higherIsBetter)
{
    const QString unit = (metricName == QStringLiteral("FWHM")) ? QStringLiteral(" px") : QString();
    const int precision = (metricName == QStringLiteral("Ecc")) ? 3 : (metricName == QStringLiteral("Stars") ? 0 : 2);

    QString tooltip =
        RegistrationTab::tr("%1 (absolute): %2%3").arg(metricName).arg(absoluteValue, 0, 'f', precision).arg(unit);

    const auto it = filterStats.find(filterKey);
    if (it == filterStats.end())
    {
        if (filterKey == QStringLiteral("(unknown)"))
        {
            tooltip += QStringLiteral("\n") +
                       RegistrationTab::tr("(filter metadata missing; filter-relative normalization unavailable)");
        }
        else if (filterKey == QStringLiteral("(no filter)"))
        {
            tooltip += QStringLiteral("\n") +
                       RegistrationTab::tr(
                           "(no filter metadata; need at least 2 succeeded frames to compute normalization stats)");
        }
        else
        {
            tooltip += QStringLiteral("\n") + RegistrationTab::tr("(no filter statistics available)");
        }
        return tooltip;
    }

    const FilterStatistics& stats = it.value();

    double median = 0.0;
    double mad = 0.0;

    if (metricName == QStringLiteral("Stars"))
    {
        median = stats.starsMedian;
        mad = stats.starsMad;
    }
    else if (metricName == QStringLiteral("FWHM"))
    {
        median = stats.fwhmMedian;
        mad = stats.fwhmMad;
    }
    else if (metricName == QStringLiteral("SNR"))
    {
        median = stats.snrMedian;
        mad = stats.snrMad;
    }
    else if (metricName == QStringLiteral("Ecc"))
    {
        median = stats.eccMedian;
        mad = stats.eccMad;
    }
    else
    {
        return tooltip;
    }

    double zScore = 0.0;
    if (mad > 1e-9)
    {
        zScore = (absoluteValue - median) / mad;
        if (!higherIsBetter)
        {
            zScore = -zScore;
        }
    }

    const double percentile = 0.5 * (1.0 + std::erf(zScore / std::sqrt(2.0)));

    QString comparison;
    if (zScore > 0.3)
    {
        comparison = RegistrationTab::tr("better than average");
    }
    else if (zScore < -0.3)
    {
        comparison = RegistrationTab::tr("worse than average");
    }
    else
    {
        comparison = RegistrationTab::tr("near average");
    }

    tooltip += QStringLiteral("\n\n");
    tooltip += RegistrationTab::tr("Filter group: %1 (%2 frames)").arg(filterKey).arg(stats.frameCount);
    tooltip += QStringLiteral("\n");
    tooltip += RegistrationTab::tr("Filter median: %1").arg(median, 0, 'f', precision);
    tooltip += QStringLiteral("\n");
    tooltip += RegistrationTab::tr("Filter MAD: %1").arg(mad, 0, 'f', 3);
    tooltip += QStringLiteral("\n\n");
    tooltip +=
        RegistrationTab::tr("Z-score: %1%2 (%3)").arg(zScore >= 0 ? "+" : "").arg(zScore, 0, 'f', 2).arg(comparison);
    tooltip += QStringLiteral("\n");
    tooltip += RegistrationTab::tr("Percentile: %1%").arg(percentile * 100.0, 0, 'f', 0);

    return tooltip;
}

[[nodiscard]] std::optional<FrameId>
selectBestReferenceFrameWithChoice(QWidget* parent, const std::unordered_map<QString, MetricRow>& metricsMap,
                                   const std::vector<FrameId>& frameIds, const AutoReferenceContext& ctx)
{
    std::vector<std::pair<FrameId, FrameQualityMetrics>> candidates;
    candidates.reserve(frameIds.size());
    for (const auto& id : frameIds)
    {
        const auto it2 = metricsMap.find(id.canonicalPath());
        if (it2 == metricsMap.end())
        {
            continue;
        }
        candidates.emplace_back(id, it2->second.metrics);
    }

    if (candidates.empty())
    {
        return std::nullopt;
    }

    AutoReferenceSelection selection;
    if (!ctx.choice.has_value())
    {
        selection.mode = AutoReferenceMode::AbsoluteAll;
    }
    else if (ctx.choice->mode == AutoReferenceChoice::Mode::SingleFilter)
    {
        selection.mode = AutoReferenceMode::AbsoluteSingleFilter;
        selection.filterName = ctx.choice->filterName;
    }
    else
    {
        selection.mode = AutoReferenceMode::FilterNormalizedAcrossFilters;
    }

    const auto filterLookup = [&](const FrameId& id) -> QString {
        const auto it = ctx.filterById.find(id);
        if (it != ctx.filterById.end() && !it->second.trimmed().isEmpty())
        {
            return it->second;
        }
        return QStringLiteral("(unknown)");
    };

    const auto best = selectBestReferenceFrameAuto(candidates, filterLookup, selection);
    if (!best.has_value() && selection.mode == AutoReferenceMode::AbsoluteSingleFilter)
    {
        openNonBlockingMessageBox(
            parent, QMessageBox::Information, RegistrationTab::tr("Auto Reference"),
            RegistrationTab::tr("No registration metrics are available for filter '%1'.").arg(selection.filterName));
    }
    return best;
}

[[nodiscard]] QString buildAutoReferenceResultText(const FrameId& bestId,
                                                   const std::unordered_map<QString, MetricRow>& metricsMap,
                                                   const std::vector<FrameId>& frameIds,
                                                   const AutoReferenceContext& ctx)
{
    const QString bestPath = bestId.canonicalPath();
    QString bestName = QFileInfo(bestPath).fileName();
    if (bestName.isEmpty())
    {
        bestName = bestId.toString();
    }

    QString filterName;
    const auto itf = ctx.filterById.find(bestId);
    if (itf != ctx.filterById.end())
    {
        filterName = itf->second;
    }

    const auto itm = metricsMap.find(bestPath);
    if (itm == metricsMap.end())
    {
        QString msg = RegistrationTab::tr("Selected reference frame:\n%1").arg(bestName);
        if (!filterName.isEmpty())
        {
            msg += RegistrationTab::tr("\nFilter: %1").arg(filterName);
        }
        msg += RegistrationTab::tr("\n\n(Metrics not found in the current metrics table.)");
        return msg;
    }

    const FrameQualityMetrics& m = itm->second.metrics;

    double score = frameQualityScore(m);
    QString scoreLabel = RegistrationTab::tr("Score (absolute)");

    if (ctx.choice.has_value() && ctx.choice->mode == AutoReferenceChoice::Mode::AllFilters)
    {
        QHash<QString, std::vector<FrameQualityMetrics>> metricsByFilter;
        for (const auto& id : frameIds)
        {
            const auto it2 = metricsMap.find(id.canonicalPath());
            if (it2 == metricsMap.end())
            {
                continue;
            }

            QString key = QStringLiteral("(unknown)");
            const auto itFilter = ctx.filterById.find(id);
            if (itFilter != ctx.filterById.end() && !itFilter->second.trimmed().isEmpty())
            {
                key = itFilter->second;
            }
            metricsByFilter[key].push_back(it2->second.metrics);
        }

        QHash<QString, std::optional<FilterStatistics>> statsByFilter;
        for (auto it = metricsByFilter.begin(); it != metricsByFilter.end(); ++it)
        {
            const auto& list = it.value();
            int succeededCount = 0;
            for (const auto& mm : list)
            {
                if (mm.succeeded)
                {
                    ++succeededCount;
                }
            }

            if (succeededCount >= 2)
            {
                statsByFilter.insert(it.key(), FilterStatisticsEngine::computeStatistics(it.key(), list));
            }
        }

        const QString bestKey = filterName.isEmpty() ? QStringLiteral("(unknown)") : filterName;
        const auto itStats = statsByFilter.find(bestKey);
        const std::optional<FilterStatistics> stats = (itStats != statsByFilter.end()) ? itStats.value() : std::nullopt;
        score = frameQualityScore(m, stats);
        scoreLabel = RegistrationTab::tr("Score (filter-normalized)");
    }

    QString msg = RegistrationTab::tr("Selected reference frame:\n%1").arg(bestName);
    if (!filterName.isEmpty())
    {
        msg += RegistrationTab::tr("\nFilter: %1").arg(filterName);
    }
    msg += RegistrationTab::tr("\n\nMetrics:\n"
                               "Stars: %1\n"
                               "FWHM: %2 px\n"
                               "SNR: %3\n"
                               "Eccentricity: %4\n"
                               "%5: %6")
               .arg(m.stars)
               .arg(m.fwhm, 0, 'f', 2)
               .arg(m.snr, 0, 'f', 2)
               .arg(m.eccentricity, 0, 'f', 3)
               .arg(scoreLabel)
               .arg(score, 0, 'f', 2);

    return msg;
}

void promptAutoReferenceChoice(QWidget* parent, const QStringList& detectedFilters,
                               std::function<void(std::optional<AutoReferenceChoice>)> chosen)
{
    if (detectedFilters.size() <= 1)
    {
        if (chosen)
        {
            chosen(std::nullopt);
        }
        return;
    }

    auto* dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(RegistrationTab::tr("Auto Reference"));

    auto* layout = new QVBoxLayout(dlg);
    auto* label = new QLabel(RegistrationTab::tr("This registration group contains multiple filters.\n\n"
                                                 "Quality metrics (especially FWHM and SNR) vary by filter, so picking "
                                                 "a reference from the wrong filter can be misleading.\n\n"
                                                 "Choose which filter to use for Auto Reference, or pick All Filters "
                                                 "to compare frames fairly using filter-normalized scoring."),
                             dlg);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto* combo = new QComboBox(dlg);
    combo->addItem(RegistrationTab::tr("All Filters (filter-normalized score)"), QVariant());
    for (const auto& filter : detectedFilters)
    {
        combo->addItem(filter, filter);
    }
    layout->addWidget(combo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    // Shown without a nested event loop: the choice is handed on once the person answers.
    QObject::connect(dlg, &QDialog::finished, dlg, [combo, chosen = std::move(chosen)](const int result) {
        if (!chosen)
        {
            return;
        }
        if (result != QDialog::Accepted)
        {
            chosen(std::nullopt);
            return;
        }

        AutoReferenceChoice choice;
        const QString selectedFilter = combo->currentData().toString().trimmed();
        if (!selectedFilter.isEmpty())
        {
            choice.mode = AutoReferenceChoice::Mode::SingleFilter;
            choice.filterName = selectedFilter;
        }
        else
        {
            choice.mode = AutoReferenceChoice::Mode::AllFilters;
        }
        chosen(choice);
    });
    dlg->open();
}

double quantileSorted(const std::vector<double>& sorted, const double q)
{
    if (sorted.empty())
    {
        return 0.0;
    }
    if (sorted.size() == 1)
    {
        return sorted.front();
    }

    const double clamped = std::clamp(q, 0.0, 1.0);
    const double pos = clamped * static_cast<double>(sorted.size() - 1);
    const auto idx = static_cast<size_t>(std::floor(pos));
    const double frac = pos - static_cast<double>(idx);
    if (idx + 1 >= sorted.size())
    {
        return sorted.back();
    }
    return sorted[idx] * (1.0 - frac) + sorted[idx + 1] * frac;
}

SuggestedThresholds suggestThresholdsFromIqr(std::vector<double> scores, std::vector<double> stars,
                                             std::vector<double> snr, std::vector<double> fwhm, std::vector<double> ecc)
{
    SuggestedThresholds out;

    const auto computeLower = [](std::vector<double>& values) -> double {
        if (values.empty())
        {
            return 0.0;
        }
        std::sort(values.begin(), values.end());
        const double q1 = quantileSorted(values, 0.25);
        const double q3 = quantileSorted(values, 0.75);
        const double iqr = q3 - q1;
        return q1 - 1.5 * iqr;
    };

    const auto computeUpper = [](std::vector<double>& values) -> double {
        if (values.empty())
        {
            return 0.0;
        }
        std::sort(values.begin(), values.end());
        const double q1 = quantileSorted(values, 0.25);
        const double q3 = quantileSorted(values, 0.75);
        const double iqr = q3 - q1;
        return q3 + 1.5 * iqr;
    };

    const double minScoreSuggested = computeLower(scores);
    const double minStarsSuggested = computeLower(stars);
    const double minSnrSuggested = computeLower(snr);
    const double maxFwhmSuggested = computeUpper(fwhm);
    const double maxEccSuggested = computeUpper(ecc);

    out.minScore = (std::max)(0.0, minScoreSuggested);
    out.minStars = (std::max)(0, static_cast<int>(std::floor(minStarsSuggested)));
    out.minSnr = (std::max)(0.0, minSnrSuggested);
    out.maxFwhm = (std::max)(0.0, maxFwhmSuggested);
    out.maxEcc = (std::max)(0.0, maxEccSuggested);
    return out;
}

GroupStatistics computeGroupStatistics(const std::vector<FrameQualityMetrics>& metrics)
{
    GroupStatistics stats;
    if (metrics.empty())
    {
        return stats;
    }

    std::vector<double> stars, fwhm, ecc, snr;
    stars.reserve(metrics.size());
    fwhm.reserve(metrics.size());
    ecc.reserve(metrics.size());
    snr.reserve(metrics.size());

    for (const auto& m : metrics)
    {
        stars.push_back(static_cast<double>(m.stars));
        fwhm.push_back(m.fwhm);
        ecc.push_back(m.eccentricity);
        snr.push_back(m.snr);
    }

    const auto sort_vec = [](std::vector<double>& v) { std::sort(v.begin(), v.end()); };
    sort_vec(stars);
    sort_vec(fwhm);
    sort_vec(ecc);
    sort_vec(snr);

    stats.starsQ1 = quantileSorted(stars, 0.25);
    stats.starsQ3 = quantileSorted(stars, 0.75);
    stats.fwhmQ1 = quantileSorted(fwhm, 0.25);
    stats.fwhmQ3 = quantileSorted(fwhm, 0.75);
    stats.eccQ1 = quantileSorted(ecc, 0.25);
    stats.eccQ3 = quantileSorted(ecc, 0.75);
    stats.snrQ1 = quantileSorted(snr, 0.25);
    stats.snrQ3 = quantileSorted(snr, 0.75);

    return stats;
}

QColor resolvedTableCellTextColor(const QTableWidget* table, const QColor& backgroundColor,
                                  const QColor& preferredTextColor)
{
    const QColor tableBase =
        table ? (table->palette().color(QPalette::Base).isValid() ? table->palette().color(QPalette::Base)
                                                                  : table->palette().color(QPalette::Window))
              : an::ui::tokens().colors.bgSurface;
    const QColor effectiveBackground = an::ui::compositeOver(backgroundColor, tableBase);
    const QColor preferred = preferredTextColor.isValid() ? preferredTextColor : an::ui::tokens().colors.textPrimary;

    return an::ui::bestContrastTextColor(effectiveBackground, preferred, QColor(255, 255, 255), QColor(0, 0, 0), 4.5);
}

QColor getMetricColor(double value, double q1, double q3, bool higherIsBetter, bool isDarkTheme)
{
    const auto& c = an::ui::tokens().colors;
    const int strongAlpha = isDarkTheme ? 108 : 44;
    const int mediumAlpha = isDarkTheme ? 92 : 38;
    const int softAlpha = isDarkTheme ? 80 : 34;
    const QColor excellentLight = ImageIntegration::Style::tint(c.statusSuccess, strongAlpha);
    const QColor goodLight = ImageIntegration::Style::tint(c.accentPrimary, mediumAlpha);
    const QColor fairLight = ImageIntegration::Style::tint(c.statusWarning, softAlpha);
    const QColor poorLight = ImageIntegration::Style::tint(c.statusError, softAlpha);
    const QColor excellentDark = excellentLight;
    const QColor goodDark = goodLight;
    const QColor fairDark = fairLight;
    const QColor poorDark = poorLight;

    const QColor& excellent = isDarkTheme ? excellentDark : excellentLight;
    const QColor& good = isDarkTheme ? goodDark : goodLight;
    const QColor& fair = isDarkTheme ? fairDark : fairLight;
    const QColor& poor = isDarkTheme ? poorDark : poorLight;

    if (std::abs(q3 - q1) < 1e-6)
    {
        return ImageIntegration::Style::tint(c.bgCardSecondary, isDarkTheme ? 230 : 190);
    }

    if (higherIsBetter)
    {
        if (value >= q3)
            return excellent;
        if (value >= (q1 + q3) / 2.0)
            return good;
        if (value >= q1)
            return fair;
        return poor;
    }

    if (value <= q1)
        return excellent;
    if (value <= (q1 + q3) / 2.0)
        return good;
    if (value <= q3)
        return fair;
    return poor;
}

QualityLevel computeFrameQuality(const FrameQualityMetrics& metrics, const GroupStatistics& stats)
{
    int score = 0;
    auto scoreMetric = [](double val, double q1, double q3, bool higherBetter) {
        if (std::abs(q3 - q1) < 1e-6)
            return 2;
        if (higherBetter)
        {
            if (val >= q3)
                return 3;
            if (val >= (q1 + q3) / 2.0)
                return 2;
            if (val >= q1)
                return 1;
            return 0;
        }
        else
        {
            if (val <= q1)
                return 3;
            if (val <= (q1 + q3) / 2.0)
                return 2;
            if (val <= q3)
                return 1;
            return 0;
        }
    };

    score += scoreMetric(static_cast<double>(metrics.stars), stats.starsQ1, stats.starsQ3, true);
    score += scoreMetric(metrics.fwhm, stats.fwhmQ1, stats.fwhmQ3, false);
    score += scoreMetric(metrics.eccentricity, stats.eccQ1, stats.eccQ3, false);
    score += scoreMetric(metrics.snr, stats.snrQ1, stats.snrQ3, true);

    if (score >= 10)
        return QualityLevel::Excellent;
    if (score >= 7)
        return QualityLevel::Good;
    if (score >= 4)
        return QualityLevel::Fair;
    return QualityLevel::Poor;
}

QString qualityIcon(QualityLevel level)
{
    switch (level)
    {
    case QualityLevel::Excellent:
        return QStringLiteral("🟢");
    case QualityLevel::Good:
        return QStringLiteral("🔵");
    case QualityLevel::Fair:
        return QStringLiteral("🟡");
    case QualityLevel::Poor:
        return QStringLiteral("🔴");
    }
    return QString();
}

QString qualityTooltip(QualityLevel level)
{
    switch (level)
    {
    case QualityLevel::Excellent:
        return RegistrationTab::tr("Excellent Quality (Top 25%)");
    case QualityLevel::Good:
        return RegistrationTab::tr("Good Quality (Above Median)");
    case QualityLevel::Fair:
        return RegistrationTab::tr("Fair Quality (Below Median)");
    case QualityLevel::Poor:
        return RegistrationTab::tr("Poor Quality (Bottom 25%)");
    }
    return QString();
}

} // namespace ImageIntegration::detail
