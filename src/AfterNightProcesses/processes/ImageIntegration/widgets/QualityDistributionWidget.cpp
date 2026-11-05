// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "QualityDistributionWidget.h"
#include "app/FileDialogUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "theme/AnDesignTokens.h"
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QToolTip>
#include <QStyleOption>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QToolButton>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QPointer>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <functional>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(QualityDistributionWidget)

namespace
{
/// Asks where a chart export goes without a nested event loop: the application's save dialog is shown, and `chosen`
/// receives the path once the person picks one, after its folder is remembered.
void selectChartExportPath(QWidget* parent, const QString& title, const QString& filter,
                           std::function<void(const QString&)> chosen)
{
    static const QString contextKey = QStringLiteral("__QUALITY_CHART_EXPORT__");
    QString initialDir = process_utils::selectPath(contextKey);
    if (!initialDir.isEmpty() && !QDir(initialDir).exists())
    {
        initialDir.clear();
    }

    auto* dialog = new QFileDialog(parent, title, initialDir, filter);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setAcceptMode(QFileDialog::AcceptSave);
    dialog->setFileMode(QFileDialog::AnyFile);
    dialog->setOptions(
        an::ui::dialogs::resolveDialogOptions(process_utils::currentGlobalSettings(), QFileDialog::Options()));
    an::ui::dialogs::detail::applyNonNativeDialogChrome(*dialog);
    QObject::connect(dialog, &QFileDialog::fileSelected, dialog, [chosen = std::move(chosen)](const QString& path) {
        if (path.isEmpty())
        {
            return;
        }
        process_utils::rememberPath(contextKey, QFileInfo(path).absolutePath());
        if (chosen)
        {
            chosen(path);
        }
    });
    dialog->open();
}

QColor blendRgb(const QColor& base, const QColor& tint, const double ratio)
{
    const double t = std::clamp(ratio, 0.0, 1.0);
    const auto lerp = [t](const int a, const int b) { return static_cast<int>(a + (b - a) * t); };

    return QColor(lerp(base.red(), tint.red()), lerp(base.green(), tint.green()), lerp(base.blue(), tint.blue()),
                  base.alpha());
}

QVector<QColor> tokenSeriesPalette()
{
    const auto& colors = an::ui::tokens().colors;
    const bool dark = an::ui::isDarkTheme();

    return {
        colors.accentPrimary,
        colors.statusWarning,
        colors.statusSuccess,
        colors.statusError,
        blendRgb(colors.accentPrimary, colors.statusInfo, dark ? 0.56 : 0.44),
        blendRgb(colors.statusWarning, colors.statusError, dark ? 0.42 : 0.34),
        blendRgb(colors.statusInfo, colors.textSecondary, dark ? 0.28 : 0.38),
        colors.textSecondary,
        blendRgb(colors.statusSuccess, colors.statusWarning, dark ? 0.46 : 0.36),
        colors.statusInfo,
    };
}

QVector<QColor> tokenOtherFilterPalette()
{
    const auto& colors = an::ui::tokens().colors;
    const bool dark = an::ui::isDarkTheme();

    return {
        blendRgb(colors.accentPrimary, colors.statusError, dark ? 0.52 : 0.42),
        colors.statusWarning,
        colors.statusInfo,
        blendRgb(colors.statusWarning, colors.textPrimary, dark ? 0.36 : 0.30),
        blendRgb(colors.statusError, colors.accentPrimary, dark ? 0.52 : 0.42),
        blendRgb(colors.statusSuccess, colors.statusWarning, dark ? 0.50 : 0.38),
        colors.textSecondary,
    };
}

// Constants
constexpr int kMarginLeft = 60;
constexpr int kMarginRight = 20;
constexpr int kMarginTop = 10;
constexpr int kMarginBottom = 40;
constexpr int kLegendGap = 10;
constexpr int kPointRadius = 3;
constexpr int kHoverRadius = 6;
constexpr int kStatsPanelHeight = 24;

struct ChartStatistics
{
    double min = 0.0;
    double max = 0.0;
    double mean = 0.0;
    double median = 0.0;
    double stdDev = 0.0;
    double q1 = 0.0;
    double q3 = 0.0;
    bool valid = false;
};

struct FilterStats
{
    double median = 0.0;
    double mad = 0.0;
};

QString normalize_filter_key(const QString& key)
{
    QString s = key.trimmed().toLower();
    s.remove(QRegularExpression(QStringLiteral("\\s+")));
    s.remove(QRegularExpression(QStringLiteral("[^a-z0-9_+\"()-]")));
    return s;
}

bool is_luminance_filter(const QString& norm)
{
    return norm == QStringLiteral("l") || norm == QStringLiteral("lum") || norm == QStringLiteral("luminance") ||
           norm.contains(QStringLiteral("lumin"));
}

bool is_red_filter(const QString& norm)
{
    return norm == QStringLiteral("r") || norm.contains(QStringLiteral("red"));
}

bool is_green_filter(const QString& norm)
{
    return norm == QStringLiteral("g") || norm.contains(QStringLiteral("green"));
}

bool is_blue_filter(const QString& norm)
{
    return norm == QStringLiteral("b") || norm.contains(QStringLiteral("blue"));
}

bool is_ha_filter(const QString& norm)
{
    return norm == QStringLiteral("ha") || norm.contains(QStringLiteral("halpha")) ||
           norm.contains(QStringLiteral("h-alpha"));
}

bool is_oiii_filter(const QString& norm)
{
    return norm == QStringLiteral("oiii") || norm.contains(QStringLiteral("o3")) ||
           norm.contains(QStringLiteral("oiii"));
}

bool is_sii_filter(const QString& norm)
{
    return norm == QStringLiteral("sii") || norm.contains(QStringLiteral("s2")) || norm.contains(QStringLiteral("sii"));
}
} // namespace

class QualityDistributionWidget::Impl
{
public:
    QualityDistributionWidget* q;

    enum class SeriesMode
    {
        ByGroup,
        ByFilter
    };

    // Data
    QVector<ChartDataPoint> sourcePoints;
    QVector<ChartGroupInfo> sourceGroups;

    QVector<ChartDataPoint> allPoints; // display points (may be rewritten based on SeriesMode)
    QVector<ChartGroupInfo> groups; // display groups (series)
    QVector<ChartDataPoint*> sortedPoints; // Pointers to elements in allPoints, sorted
    ChartStatistics stats;
    QHash<QString, FilterStats> filterStatsMap; // Per-filter stats for normalization

    // Config
    QualityMetric currentMetric = QualityMetric::Score;
    PlotStyle currentPlotStyle = PlotStyle::Points;
    SortMode currentSortMode = SortMode::ByIndex;
    NormalizeMode currentNormalizeMode = NormalizeMode::Off;
    SeriesMode currentSeriesMode = SeriesMode::ByGroup;
    bool showPercentileBand = true; // Default to true per plan
    bool showDropToBaseline = false;
    bool showMedianLines = true;
    bool showSelectedGuides = true;

    // Thresholds
    double minScoreThreshold = 0.0;
    int minStarsThreshold = 0;
    double minSnrThreshold = 0.0;
    double maxFwhmThreshold = 0.0;
    double maxEccThreshold = 0.0;

    // State
    QString selectedFrameId;
    int hoveredPointIndex = -1;
    QRect plotRect;
    QRect legendAreaRect;
    bool legendDockedRight = false;
    double minY = 0.0;
    double maxY = 1.0;

    // UI Controls
    QComboBox* comboMetric = nullptr;
    QComboBox* comboPlotStyle = nullptr;
    QComboBox* comboSort = nullptr;
    QComboBox* comboNormalize = nullptr;
    QCheckBox* checkPercentileBand = nullptr;
    QCheckBox* checkMedianLines = nullptr;
    QCheckBox* checkSelectedGuides = nullptr;
    QCheckBox* checkGroupByFilter = nullptr;
    QCheckBox* checkDropToBaseline = nullptr;
    QWidget* plotArea = nullptr; // Custom widget for painting
    QHBoxLayout* controlsLayout = nullptr;

    Impl(QualityDistributionWidget* parent) : q(parent) {}

    void setupUi()
    {
        auto* layout = new QVBoxLayout(q);
        layout->setContentsMargins(6, 6, 6, 4);
        layout->setSpacing(0);

        // Control Bar
        controlsLayout = new QHBoxLayout();
        controlsLayout->setContentsMargins(4, 4, 4, 4);
        controlsLayout->setSpacing(8);

        // Metric Selector
        controlsLayout->addWidget(new QLabel(q->tr("Metric:"), q));
        comboMetric = new QComboBox(q);
        comboMetric->addItem(q->tr("Score"), QVariant::fromValue(QualityMetric::Score));
        comboMetric->addItem(q->tr("Stars"), QVariant::fromValue(QualityMetric::Stars));
        comboMetric->addItem(q->tr("FWHM"), QVariant::fromValue(QualityMetric::FWHM));
        comboMetric->addItem(q->tr("SNR"), QVariant::fromValue(QualityMetric::SNR));
        comboMetric->addItem(q->tr("Eccentricity"), QVariant::fromValue(QualityMetric::Eccentricity));
        comboMetric->setCurrentIndex(0); // Score
        QObject::connect(comboMetric, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int) {
            currentMetric = comboMetric->currentData().value<QualityMetric>();
            resortData();
            q->update();
        });
        controlsLayout->addWidget(comboMetric);

        // Plot Style Selector
        controlsLayout->addWidget(new QLabel(q->tr("Plot:"), q));
        comboPlotStyle = new QComboBox(q);
        comboPlotStyle->addItem(q->tr("Points"), QVariant::fromValue(PlotStyle::Points));
        comboPlotStyle->addItem(q->tr("Line"), QVariant::fromValue(PlotStyle::Line));
        comboPlotStyle->addItem(q->tr("Line + Points"), QVariant::fromValue(PlotStyle::LinePoints));
        comboPlotStyle->addItem(q->tr("Area"), QVariant::fromValue(PlotStyle::Area));
        comboPlotStyle->addItem(q->tr("Bars"), QVariant::fromValue(PlotStyle::Bars));
        comboPlotStyle->setCurrentText(q->tr("Points"));
        QObject::connect(comboPlotStyle, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int) {
            currentPlotStyle = comboPlotStyle->currentData().value<PlotStyle>();
            // Range depends on plot style (e.g., area/bars baseline)
            computeRange();
            q->update();
        });
        controlsLayout->addWidget(comboPlotStyle);

        // Sort Selector
        controlsLayout->addWidget(new QLabel(q->tr("Sort:"), q));
        comboSort = new QComboBox(q);
        comboSort->addItem(q->tr("By Index"), QVariant::fromValue(SortMode::ByIndex));
        comboSort->addItem(q->tr("By Value (Asc)"), QVariant::fromValue(SortMode::ByValueAsc));
        comboSort->addItem(q->tr("By Value (Desc)"), QVariant::fromValue(SortMode::ByValueDesc));
        comboSort->addItem(q->tr("By Capture Time"), QVariant::fromValue(SortMode::ByCaptureTime));
        comboSort->setCurrentText(q->tr("By Index"));
        QObject::connect(comboSort, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int) {
            currentSortMode = comboSort->currentData().value<SortMode>();
            resortData();
            q->update();
        });
        controlsLayout->addWidget(comboSort);

        // Normalize Selector
        controlsLayout->addWidget(new QLabel(q->tr("Norm:"), q));
        comboNormalize = new QComboBox(q);
        comboNormalize->addItem(q->tr("Off"), QVariant::fromValue(NormalizeMode::Off));
        comboNormalize->addItem(q->tr("By Filter"), QVariant::fromValue(NormalizeMode::ByFilter));
        QObject::connect(comboNormalize, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int) {
            currentNormalizeMode = comboNormalize->currentData().value<NormalizeMode>();
            resortData();
            q->update();
        });
        controlsLayout->addWidget(comboNormalize);

        // Overlays
        checkPercentileBand = new QCheckBox(q->tr("Percentile band"), q);
        checkPercentileBand->setChecked(true);
        checkPercentileBand->setToolTip(q->tr("Show the 25th–75th percentile band (Q1–Q3)."));
        QObject::connect(checkPercentileBand, &QCheckBox::toggled, [this](bool checked) {
            showPercentileBand = checked;
            q->update();
        });
        controlsLayout->addWidget(checkPercentileBand);

        checkMedianLines = new QCheckBox(q->tr("Median lines"), q);
        checkMedianLines->setChecked(true);
        checkMedianLines->setToolTip(q->tr("Show dashed median reference lines (one per group)."));
        QObject::connect(checkMedianLines, &QCheckBox::toggled, [this](bool checked) {
            showMedianLines = checked;
            q->update();
        });
        controlsLayout->addWidget(checkMedianLines);

        checkSelectedGuides = new QCheckBox(q->tr("Selection guides"), q);
        checkSelectedGuides->setChecked(true);
        checkSelectedGuides->setToolTip(q->tr("Show guide lines for the currently selected frame."));
        QObject::connect(checkSelectedGuides, &QCheckBox::toggled, [this](bool checked) {
            showSelectedGuides = checked;
            q->update();
        });
        controlsLayout->addWidget(checkSelectedGuides);

        // Export Button
        auto* btnExport = new QToolButton(q);
        btnExport->setText(q->tr("Export..."));
        btnExport->setPopupMode(QToolButton::InstantPopup);
        auto* exportMenu = new QMenu(btnExport);

        QAction* copyAction = exportMenu->addAction(q->tr("Copy Chart Image"));
        QObject::connect(copyAction, &QAction::triggered, q, &QualityDistributionWidget::copyChartImageToClipboard);

        QAction* saveImageAction = exportMenu->addAction(q->tr("Save Chart as Image..."));
        QObject::connect(saveImageAction, &QAction::triggered, q, [this]() {
            const QPointer<QualityDistributionWidget> widget(q);
            selectChartExportPath(q, q->tr("Save Chart Image"), q->tr("Images (*.png *.jpg)"),
                                  [widget](const QString& path) {
                                      if (widget)
                                      {
                                          widget->exportChartImage(path);
                                      }
                                  });
        });

        QAction* exportCsvAction = exportMenu->addAction(q->tr("Export Data to CSV..."));
        QObject::connect(exportCsvAction, &QAction::triggered, q, [this]() {
            const QPointer<QualityDistributionWidget> widget(q);
            selectChartExportPath(q, q->tr("Export CSV"), q->tr("CSV Files (*.csv)"), [widget](const QString& path) {
                if (widget)
                {
                    widget->exportChartDataToCsv(path);
                }
            });
        });

        btnExport->setMenu(exportMenu);
        controlsLayout->addWidget(btnExport);

        // Right-aligned grouping controls
        controlsLayout->addStretch();

        checkGroupByFilter = new QCheckBox(q->tr("Group by filter"), q);
        checkGroupByFilter->setChecked(false);
        checkGroupByFilter->setToolTip(q->tr("Split the series by filter (requires at least 2 different filters)."));
        QObject::connect(checkGroupByFilter, &QCheckBox::toggled, [this](bool checked) {
            currentSeriesMode = checked ? SeriesMode::ByFilter : SeriesMode::ByGroup;
            updateSeriesUiAvailability();
            rebuildSeriesData();
            updateThresholdStatus();
            resortData();
            q->update();
        });
        controlsLayout->addWidget(checkGroupByFilter);

        checkDropToBaseline = new QCheckBox(q->tr("Drop to baseline"), q);
        checkDropToBaseline->setChecked(false);
        checkDropToBaseline->setToolTip(q->tr("When grouping by filter, draw segment start/end down to the baseline to "
                                              "avoid long jumps across missing frames."));
        QObject::connect(checkDropToBaseline, &QCheckBox::toggled, [this](bool checked) {
            showDropToBaseline = checked;
            q->update();
        });
        controlsLayout->addWidget(checkDropToBaseline);
        layout->addLayout(controlsLayout);

        // Push controls to top
        layout->addStretch();
    }

    void updateSeriesUiAvailability()
    {
        if (!checkGroupByFilter)
            return;

        QSet<QString> uniqueFilters;
        for (const auto& p : sourcePoints)
        {
            QString key = p.filterName;
            if (key.isEmpty())
                key = QStringLiteral("(unknown)");
            uniqueFilters.insert(key);
            if (uniqueFilters.size() >= 2)
                break;
        }

        const bool canGroupByFilter = (uniqueFilters.size() >= 2);
        checkGroupByFilter->setEnabled(canGroupByFilter);
        if (!canGroupByFilter)
        {
            checkGroupByFilter->setToolTip(
                q->tr("Grouping by filter requires at least 2 different filter values in this registration group."));
            // Force disabled/off if not available.
            currentSeriesMode = SeriesMode::ByGroup;
            const QSignalBlocker blocker(checkGroupByFilter);
            checkGroupByFilter->setChecked(false);
        }
        else
        {
            checkGroupByFilter->setToolTip(QString());
            currentSeriesMode = checkGroupByFilter->isChecked() ? SeriesMode::ByFilter : SeriesMode::ByGroup;
        }

        if (checkDropToBaseline)
        {
            const bool enabled = canGroupByFilter && (currentSeriesMode == SeriesMode::ByFilter);
            checkDropToBaseline->setEnabled(enabled);
            if (enabled)
            {
                checkDropToBaseline->setToolTip(q->tr("When grouping by filter, draw segment start/end down to the "
                                                      "baseline to avoid long jumps across missing frames."));
            }
            else
            {
                checkDropToBaseline->setToolTip(q->tr("Only applies when grouping by filter."));
            }
        }
    }

    void rebuildSeriesData()
    {
        allPoints = sourcePoints;
        groups = sourceGroups;

        if (currentSeriesMode != SeriesMode::ByFilter)
        {
            return;
        }

        // Build groups by filter name and rewrite each point's groupKey to be the filter key.
        QHash<QString, int> groupIndexByFilter;
        QVector<ChartGroupInfo> filterGroups;
        filterGroups.reserve(8);

        auto pickFilterColor = [&](const QString& filterKey, int fallbackIndex) -> QColor {
            const QString norm = normalize_filter_key(filterKey);
            const auto& colors = an::ui::tokens().colors;
            const QVector<QColor> otherFilterPalette = tokenOtherFilterPalette();

            // The user expectation is: R=red, G=green, B=blue, L=white.
            // For Luminance in light theme, pure white is invisible on white plot background,
            // so we use the palette text color (black on light theme, white on dark theme).
            if (is_luminance_filter(norm))
            {
                return q->palette().text().color();
            }
            if (is_red_filter(norm))
            {
                return colors.statusError;
            }
            if (is_green_filter(norm))
            {
                return colors.statusSuccess;
            }
            if (is_blue_filter(norm))
            {
                return colors.accentPrimary;
            }

            // Common narrowband defaults.
            if (is_ha_filter(norm))
            {
                return blendRgb(colors.statusError, colors.accentPrimary, 0.45);
            }
            if (is_oiii_filter(norm))
            {
                return colors.statusInfo;
            }
            if (is_sii_filter(norm))
            {
                return colors.statusWarning;
            }

            return otherFilterPalette[fallbackIndex % otherFilterPalette.size()];
        };

        auto ensureGroup = [&](const QString& filterKey) -> int {
            auto it = groupIndexByFilter.find(filterKey);
            if (it != groupIndexByFilter.end())
                return it.value();

            const int idx = filterGroups.size();
            ChartGroupInfo info;
            info.key = filterKey;
            info.displayName = filterKey;
            info.color = pickFilterColor(filterKey, idx);
            filterGroups.append(info);
            groupIndexByFilter.insert(filterKey, idx);
            return idx;
        };

        for (auto& p : allPoints)
        {
            QString filterKey = p.filterName;
            if (filterKey.isEmpty())
                filterKey = QStringLiteral("(unknown)");

            const int idx = ensureGroup(filterKey);
            p.groupKey = filterKey;
            filterGroups[idx].frameCount++;
            if (p.isExcluded)
                filterGroups[idx].excludedCount++;
            if (p.isReference)
                filterGroups[idx].hasReference = true;
        }

        groups = std::move(filterGroups);
    }

    QString metricName() const
    {
        switch (currentMetric)
        {
        case QualityMetric::Score:
            return QObject::tr("Score");
        case QualityMetric::Stars:
            return QObject::tr("Stars");
        case QualityMetric::FWHM:
            return QObject::tr("FWHM");
        case QualityMetric::SNR:
            return QObject::tr("SNR");
        case QualityMetric::Eccentricity:
            return QObject::tr("Eccentricity");
        }
        return {};
    }

    double getRawValue(const ChartDataPoint& p) const
    {
        switch (currentMetric)
        {
        case QualityMetric::Score:
            return p.score;
        case QualityMetric::Stars:
            return static_cast<double>(p.stars);
        case QualityMetric::FWHM:
            return p.fwhm;
        case QualityMetric::SNR:
            return p.snr;
        case QualityMetric::Eccentricity:
            return p.eccentricity;
        }
        return 0.0;
    }

    double getValue(const ChartDataPoint& p) const
    {
        double raw = getRawValue(p);

        if (currentNormalizeMode == NormalizeMode::ByFilter)
        {
            QString key = p.filterName;
            if (key.isEmpty())
                key = "(unknown)";

            auto it = filterStatsMap.find(key);
            if (it != filterStatsMap.end() && it->mad > 1e-9)
            {
                // Z-score
                double z = (raw - it->median) / it->mad;
                // Invert for FWHM/Ecc (lower is better, so higher Z means worse)
                // Actually, standard Z-score is (x - u) / s.
                // If FWHM is 5 and median is 3, Z is positive. Positive means "high FWHM" (bad).
                // If SNR is 50 and median is 30, Z is positive. Positive means "high SNR" (good).
                // Usually charts show "higher is up".
                // If we want consistency where UP is GOOD, we should invert FWHM/Ecc.
                // But the user might expect raw Z-scores.
                // Let's keep raw Z-scores.
                return z;
            }
            return 0.0; // Fallback
        }
        return raw;
    }

    void computeFilterStats()
    {
        filterStatsMap.clear();
        if (currentNormalizeMode != NormalizeMode::ByFilter)
            return;

        QHash<QString, std::vector<double>> valuesByFilter;
        for (const auto& p : allPoints)
        {
            QString key = p.filterName;
            if (key.isEmpty())
                key = "(unknown)";
            valuesByFilter[key].push_back(getRawValue(p));
        }

        for (auto it = valuesByFilter.begin(); it != valuesByFilter.end(); ++it)
        {
            std::vector<double>& vals = it.value();
            if (vals.empty())
                continue;

            // Median
            size_t n = vals.size();
            size_t mid = n / 2;
            std::nth_element(vals.begin(), vals.begin() + mid, vals.end());
            double median = vals[mid];

            // MAD
            std::vector<double> absDevs;
            absDevs.reserve(n);
            for (double v : vals)
                absDevs.push_back(std::abs(v - median));
            std::nth_element(absDevs.begin(), absDevs.begin() + mid, absDevs.end());
            double mad = absDevs[mid];

            // Scale MAD to estimate sigma (multiply by 1.4826 for normal distribution consistency)
            mad *= 1.4826;

            filterStatsMap.insert(it.key(), {median, mad});
        }
    }

    void computeStatistics()
    {
        if (sortedPoints.isEmpty())
        {
            stats = ChartStatistics();
            return;
        }

        std::vector<double> values;
        values.reserve(sortedPoints.size());
        for (const auto* p : sortedPoints)
        {
            values.push_back(getValue(*p));
        }

        // Basic stats
        stats.valid = true;
        stats.min = *std::min_element(values.begin(), values.end());
        stats.max = *std::max_element(values.begin(), values.end());

        double sum = std::accumulate(values.begin(), values.end(), 0.0);
        stats.mean = sum / values.size();

        // StdDev
        double sqSum = std::inner_product(values.begin(), values.end(), values.begin(), 0.0);
        stats.stdDev = std::sqrt(sqSum / values.size() - stats.mean * stats.mean);

        // Median and Quartiles
        std::sort(values.begin(), values.end());
        auto quantile = [&](double q) {
            if (values.empty())
                return 0.0;
            double pos = q * (values.size() - 1);
            size_t idx = static_cast<size_t>(pos);
            double frac = pos - idx;
            if (idx + 1 < values.size())
            {
                return values[idx] * (1.0 - frac) + values[idx + 1] * frac;
            }
            return values[idx];
        };

        stats.median = quantile(0.5);
        stats.q1 = quantile(0.25);
        stats.q3 = quantile(0.75);
    }

    void applyThresholds(QVector<ChartDataPoint>& points)
    {
        for (auto& p : points)
        {
            if (!p.hasMetrics)
            {
                p.failsThreshold = false;
                continue;
            }

            bool fails = false;
            if (minScoreThreshold > 0.0 && p.score < minScoreThreshold)
                fails = true;
            if (minStarsThreshold > 0 && p.stars < minStarsThreshold)
                fails = true;
            if (minSnrThreshold > 0.0 && p.snr < minSnrThreshold)
                fails = true;
            if (maxFwhmThreshold > 0.0 && p.fwhm > maxFwhmThreshold)
                fails = true;
            if (maxEccThreshold > 0.0 && p.eccentricity > maxEccThreshold)
                fails = true;
            p.failsThreshold = fails;
        }
    }

    void updateThresholdStatus()
    {
        applyThresholds(sourcePoints);
        applyThresholds(allPoints);
    }

    void resortData()
    {
        computeFilterStats(); // Prepare normalization stats if needed

        sortedPoints.clear();
        sortedPoints.reserve(allPoints.size());

        for (int i = 0; i < allPoints.size(); ++i)
        {
            sortedPoints.append(&allPoints[i]);
        }

        switch (currentSortMode)
        {
        case SortMode::ByIndex:
            // Keep original order
            break;
        case SortMode::ByValueAsc:
            std::sort(sortedPoints.begin(), sortedPoints.end(),
                      [this](const ChartDataPoint* a, const ChartDataPoint* b) { return getValue(*a) < getValue(*b); });
            break;
        case SortMode::ByValueDesc:
            std::sort(sortedPoints.begin(), sortedPoints.end(),
                      [this](const ChartDataPoint* a, const ChartDataPoint* b) { return getValue(*a) > getValue(*b); });
            break;
        case SortMode::ByCaptureTime:
            std::sort(sortedPoints.begin(), sortedPoints.end(),
                      [](const ChartDataPoint* a, const ChartDataPoint* b) { return a->captureTime < b->captureTime; });
            break;
        }

        computeRange();
        computeStatistics();
    }

    void computeRange()
    {
        if (sortedPoints.empty())
        {
            minY = 0.0;
            maxY = 1.0;
            return;
        }

        minY = std::numeric_limits<double>::max();
        maxY = std::numeric_limits<double>::lowest();

        for (const auto* p : sortedPoints)
        {
            double v = getValue(*p);
            if (v < minY)
                minY = v;
            if (v > maxY)
                maxY = v;
        }

        // Add padding
        double range = maxY - minY;
        if (range < 1e-6)
            range = 1.0;

        // If showing percentile band, ensure Q1/Q3 are somewhat visible (not strictly required but good)
        // For Area/Bars with 0 baseline, minY should start at 0 if values are positive.
        if (currentPlotStyle == PlotStyle::Area || currentPlotStyle == PlotStyle::Bars)
        {
            if (minY > 0)
                minY = 0;
        }
        else
        {
            minY -= range * 0.1;
        }
        maxY += range * 0.1;

        // Ensure threshold lines are visible if active for current metric
        if (currentMetric == QualityMetric::Stars && minStarsThreshold > 0)
        {
            minY = (std::min)(minY, static_cast<double>(minStarsThreshold) * 0.9);
            maxY = (std::max)(maxY, static_cast<double>(minStarsThreshold) * 1.1);
        }
        else if (currentMetric == QualityMetric::FWHM && maxFwhmThreshold > 0.0)
        {
            minY = (std::min)(minY, maxFwhmThreshold * 0.9);
            maxY = (std::max)(maxY, maxFwhmThreshold * 1.1);
        }
        else if (currentMetric == QualityMetric::Eccentricity && maxEccThreshold > 0.0)
        {
            minY = (std::min)(minY, maxEccThreshold * 0.9);
            maxY = (std::max)(maxY, maxEccThreshold * 1.1);
        }

        // For some metrics, 0 is a hard floor
        if (currentMetric == QualityMetric::Stars || currentMetric == QualityMetric::FWHM ||
            currentMetric == QualityMetric::SNR || currentMetric == QualityMetric::Eccentricity)
        {
            if (minY < 0)
                minY = 0;
        }
    }

    QPointF dataToScreen(int index, double value) const
    {
        if (sortedPoints.isEmpty())
            return QPointF();

        // For a single point, center it; otherwise distribute evenly.
        double xStep = (sortedPoints.size() <= 1) ? 0.0 : (plotRect.width()) / (double)(sortedPoints.size() - 1);

        double x = (sortedPoints.size() == 1) ? plotRect.center().x() : plotRect.left() + index * xStep;

        // Invert Y for screen coords
        double yRange = maxY - minY;
        if (yRange < 1e-9)
            yRange = 1.0;
        double normalizedY = (value - minY) / yRange;
        double y = plotRect.bottom() - normalizedY * plotRect.height();

        return QPointF(x, y);
    }

    double valueToScreenY(double value) const
    {
        double yRange = maxY - minY;
        if (yRange < 1e-9)
            yRange = 1.0;
        double normalizedY = (value - minY) / yRange;
        return plotRect.bottom() - normalizedY * plotRect.height();
    }

    QString groupDisplayName(const QString& groupKey) const
    {
        for (const auto& g : groups)
        {
            if (g.key == groupKey)
                return g.displayName;
        }
        return groupKey;
    }

    QString buildTooltipHtml(const ChartDataPoint& pt) const
    {
        QStringList lines;
        lines << QString("<b>%1</b>").arg(pt.displayName);
        lines << QString("Stars: %1").arg(pt.stars);
        lines << QString("FWHM: %1").arg(pt.fwhm, 0, 'f', 2);
        lines << QString("SNR: %1").arg(pt.snr, 0, 'f', 2);
        lines << QString("Ecc: %1").arg(pt.eccentricity, 0, 'f', 3);
        lines << QString("Score: %1").arg(pt.score, 0, 'f', 2);

        if (!pt.groupKey.isEmpty())
        {
            lines << QString("Group: %1").arg(groupDisplayName(pt.groupKey));
        }

        if (pt.isReference)
            lines << tr("<i>Reference frame</i>");
        if (pt.isExcluded)
            lines << tr("<i>Excluded</i>");
        if (pt.failsThreshold)
            lines << tr("<i>Fails threshold</i>");

        return lines.join("<br>");
    }

    QColor getGroupColor(const QString& groupKey) const
    {
        for (const auto& g : groups)
        {
            if (g.key == groupKey)
                return g.color;
        }
        const QVector<QColor> palette = tokenSeriesPalette();
        return palette.isEmpty() ? q->palette().highlight().color() : palette[0];
    }

    int legendDockWidthHintPx(QPainter& p) const
    {
        if (sortedPoints.isEmpty())
        {
            return 0;
        }

        bool hasReference =
            std::any_of(allPoints.begin(), allPoints.end(), [](const ChartDataPoint& pt) { return pt.isReference; });
        bool hasExcluded =
            std::any_of(allPoints.begin(), allPoints.end(), [](const ChartDataPoint& pt) { return pt.isExcluded; });
        bool hasThreshold = (currentMetric == QualityMetric::Stars && minStarsThreshold > 0) ||
                            (currentMetric == QualityMetric::FWHM && maxFwhmThreshold > 0.0) ||
                            (currentMetric == QualityMetric::Eccentricity && maxEccThreshold > 0.0);

        QStringList legendItems;
        if (showPercentileBand && stats.valid)
            legendItems << QObject::tr("▒ Q1–Q3");
        if (showMedianLines)
            legendItems << QObject::tr("┄ Median");
        if (hasReference)
            legendItems << QObject::tr("★ Reference");
        if (hasExcluded)
            legendItems << QObject::tr("✕ Excluded");
        if (hasThreshold)
            legendItems << QObject::tr("─ ─ Threshold");

        if (groups.size() > 1)
        {
            for (const auto& g : groups)
            {
                legendItems << g.displayName;
            }
        }

        if (legendItems.isEmpty())
        {
            return 0;
        }

        QFont font = an::ui::tokens().typography.caption;
        const QFontMetrics fm(font);

        int maxTextWidth = 0;
        for (const auto& text : legendItems)
        {
            maxTextWidth = (std::max)(maxTextWidth, fm.horizontalAdvance(text));
        }

        // Match paintLegend's sizing (roughly).
        const int totalWidth = maxTextWidth + 24;
        const int bgWidth = totalWidth + 12;

        // Add a small padding around the legend box when docking.
        const int padded = bgWidth + 12;
        return std::clamp(padded, 140, 240);
    }

    void paintSelectedFrameLine(QPainter& p)
    {
        if (!showSelectedGuides)
        {
            return;
        }

        const QString selected = selectedFrameId.trimmed();
        if (sortedPoints.isEmpty() || selected.isEmpty())
        {
            return;
        }

        int selectedIndex = -1;
        const ChartDataPoint* selectedPoint = nullptr;
        for (int i = 0; i < sortedPoints.size(); ++i)
        {
            const auto* dp = sortedPoints[i];
            if (!dp)
            {
                continue;
            }
            if (dp->frameId == selected)
            {
                selectedIndex = i;
                selectedPoint = dp;
                break;
            }
        }

        if (selectedIndex < 0 || !selectedPoint)
        {
            return;
        }

        const QPointF pt = dataToScreen(selectedIndex, getValue(*selectedPoint));

        QColor lineColor = q->palette().highlight().color();
        lineColor.setAlpha(160);

        QPen pen(lineColor, 2, Qt::DashLine);
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(pt.x(), plotRect.top()), QPointF(pt.x(), plotRect.bottom()));

        // Horizontal guide at the selected frame's metric value.
        p.drawLine(QPointF(plotRect.left(), pt.y()), QPointF(plotRect.right(), pt.y()));
    }

    void paint(QPainter& p)
    {
        // Calculate plot rect (area below controls)
        int controlsHeight = 0;
        if (controlsLayout)
        {
            controlsHeight = controlsLayout->geometry().bottom() + 10;
        }

        legendAreaRect = QRect();
        legendDockedRight = false;

        const QRect fullRect = q->rect();
        const int bottomAdjust = (kMarginBottom + kStatsPanelHeight);
        const int contentWidth = fullRect.width() - kMarginLeft - kMarginRight;

        int legendReserve = legendDockWidthHintPx(p);
        if (legendReserve > 0)
        {
            // Only dock if we can keep a reasonable plot width.
            constexpr int kMinPlotWidth = 220;
            if ((contentWidth - legendReserve - kLegendGap) >= kMinPlotWidth)
            {
                legendDockedRight = true;
            }
            else
            {
                legendReserve = 0;
            }
        }

        plotRect =
            fullRect.adjusted(kMarginLeft, controlsHeight + kMarginTop,
                              -(kMarginRight + (legendDockedRight ? (legendReserve + kLegendGap) : 0)), -bottomAdjust);

        if (legendDockedRight && legendReserve > 0)
        {
            legendAreaRect = QRect(plotRect.right() + kLegendGap + 1, plotRect.top(), legendReserve, plotRect.height());
        }

        // Background
        p.fillRect(q->rect(), q->palette().window());

        // Draw Plot Background
        p.fillRect(plotRect, q->palette().base());
        p.setPen(q->palette().color(QPalette::Mid));
        p.drawRect(plotRect);

        if (legendDockedRight && legendAreaRect.isValid())
        {
            // Keep the legend area visually separate but unobtrusive.
            p.fillRect(legendAreaRect, q->palette().window());
        }

        if (sortedPoints.isEmpty())
        {
            p.setPen(q->palette().text().color());
            p.drawText(plotRect, Qt::AlignCenter,
                       q->tr("No metrics available. Run registration to populate this chart."));
            return;
        }

        // Draw Grid & Axes
        paintGridAndAxes(p);

        // Draw Threshold Lines (Background layer)
        paintThresholdLines(p);

        // Draw Percentile Band
        if (showPercentileBand && stats.valid)
        {
            paintPercentileBand(p);
        }

        // Draw Median Lines
        if (showMedianLines)
        {
            paintMedianLines(p);
        }

        // Draw Data
        if (currentPlotStyle == PlotStyle::Area)
        {
            // Draw per group
            for (const auto& group : groups)
            {
                paintAreaForGroup(p, group.key);
            }
        }
        else if (currentPlotStyle == PlotStyle::Bars)
        {
            paintBars(p);
        }
        else if (currentPlotStyle == PlotStyle::Line || currentPlotStyle == PlotStyle::LinePoints)
        {
            // Draw lines per group
            for (const auto& group : groups)
            {
                QPainterPath path;
                bool hasAny = false;
                int prevIndex = -999999;
                QPointF lastPt;
                const bool dropToBaseline = (currentSeriesMode == SeriesMode::ByFilter) && showDropToBaseline;
                // Use the 0-axis if it is within range, otherwise clamp to the bottom of the plot area.
                const double baselineY =
                    std::clamp(valueToScreenY(0.0), (double)plotRect.top(), (double)plotRect.bottom());
                QColor color = getGroupColor(group.key);

                for (int i = 0; i < sortedPoints.size(); ++i)
                {
                    if (sortedPoints[i]->groupKey != group.key)
                        continue;

                    QPointF pt = dataToScreen(i, getValue(*sortedPoints[i]));

                    // Break the path when this series has a gap on the X axis.
                    // This avoids ugly long straight segments when, e.g., a filter is missing for a run of frames.
                    if (!hasAny)
                    {
                        if (dropToBaseline)
                        {
                            path.moveTo(pt.x(), baselineY);
                            path.lineTo(pt);
                        }
                        else
                        {
                            path.moveTo(pt);
                        }
                    }
                    else if (i != (prevIndex + 1))
                    {
                        // Close the previous segment down to baseline (optional)
                        if (dropToBaseline)
                        {
                            path.lineTo(lastPt.x(), baselineY);
                        }
                        // Start new segment
                        if (dropToBaseline)
                        {
                            path.moveTo(pt.x(), baselineY);
                            path.lineTo(pt);
                        }
                        else
                        {
                            path.moveTo(pt);
                        }
                    }
                    else
                    {
                        path.lineTo(pt);
                    }
                    hasAny = true;
                    prevIndex = i;
                    lastPt = pt;
                }

                if (hasAny && dropToBaseline)
                {
                    // Close last segment
                    path.lineTo(lastPt.x(), baselineY);
                }

                if (hasAny)
                {
                    p.setPen(QPen(color, 2));
                    p.setRenderHint(QPainter::Antialiasing);
                    p.setBrush(Qt::NoBrush);
                    p.drawPath(path);
                }
            }
        }

        // Always show key markers (reference/excluded/selected), even when not rendering all points.
        if (currentPlotStyle != PlotStyle::Points && currentPlotStyle != PlotStyle::LinePoints)
        {
            paintSpecialMarkers(p);
        }

        if (currentPlotStyle == PlotStyle::Points || currentPlotStyle == PlotStyle::LinePoints)
        {
            for (int i = 0; i < sortedPoints.size(); ++i)
            {
                const auto* dp = sortedPoints[i];
                QPointF pt = dataToScreen(i, getValue(*dp));

                QColor color = getGroupColor(dp->groupKey);

                // Markers
                if (dp->isReference)
                {
                    // Star marker
                    p.setBrush(an::ui::tokens().colors.statusWarning);
                    p.setPen(an::ui::tokens().colors.textPrimary);
                    drawStar(p, pt, 8);
                }
                else if (dp->isExcluded)
                {
                    // X marker
                    p.setPen(QPen(an::ui::tokens().colors.statusError, 2));
                    p.drawLine(pt + QPointF(-4, -4), pt + QPointF(4, 4));
                    p.drawLine(pt + QPointF(-4, 4), pt + QPointF(4, -4));
                }
                else
                {
                    // Normal point
                    if (dp->failsThreshold)
                    {
                        p.setBrush(an::ui::tokens().colors.statusWarning);
                    }
                    else
                    {
                        p.setBrush(color);
                    }

                    if (dp->frameId == selectedFrameId)
                    {
                        p.setPen(QPen(q->palette().highlight(), 2));
                        p.drawEllipse(pt, kPointRadius + 2, kPointRadius + 2);
                    }
                    else
                    {
                        p.setPen(Qt::NoPen);
                        p.drawEllipse(pt, kPointRadius, kPointRadius);
                    }
                }
            }
        }

        // Highlight hovered point
        if (hoveredPointIndex >= 0 && hoveredPointIndex < sortedPoints.size())
        {
            QPointF pt = dataToScreen(hoveredPointIndex, getValue(*sortedPoints[hoveredPointIndex]));
            p.setPen(QPen(q->palette().text().color(), 1, Qt::DashLine));
            // Vertical line
            p.drawLine(pt.x(), plotRect.top(), pt.x(), plotRect.bottom());
            // Horizontal line
            p.drawLine(plotRect.left(), pt.y(), plotRect.right(), pt.y());
        }

        // Selected frame indicator (persistent, even when not hovering)
        paintSelectedFrameLine(p);

        paintStatisticsPanel(p);
        paintLegend(p);
    }

    void paintSpecialMarkers(QPainter& p)
    {
        if (sortedPoints.isEmpty())
            return;

        p.setRenderHint(QPainter::Antialiasing);

        for (int i = 0; i < sortedPoints.size(); ++i)
        {
            const auto* dp = sortedPoints[i];
            QPointF pt = dataToScreen(i, getValue(*dp));

            if (dp->isReference)
            {
                p.setBrush(an::ui::tokens().colors.statusWarning);
                p.setPen(an::ui::tokens().colors.textPrimary);
                drawStar(p, pt, 8);
                continue;
            }

            if (dp->isExcluded)
            {
                p.setPen(QPen(an::ui::tokens().colors.statusError, 2));
                p.drawLine(pt + QPointF(-4, -4), pt + QPointF(4, 4));
                p.drawLine(pt + QPointF(-4, 4), pt + QPointF(4, -4));
                continue;
            }

            if (dp->frameId == selectedFrameId)
            {
                QColor color = getGroupColor(dp->groupKey);
                p.setBrush(color);
                p.setPen(QPen(q->palette().highlight(), 2));
                p.drawEllipse(pt, kPointRadius + 2, kPointRadius + 2);
            }
        }
    }

    void paintGridAndAxes(QPainter& p)
    {
        p.setRenderHint(QPainter::Antialiasing, false);
        QColor gridColor = q->palette().color(QPalette::Midlight);
        gridColor.setAlpha(100);

        // Y Axis Labels and Grid
        int numTicksY = 5;
        QFont font = p.font();
        // font.setPointSize(font.pointSize()); // Use default size
        p.setFont(font);

        for (int i = 0; i <= numTicksY; ++i)
        {
            double val = minY + (maxY - minY) * i / numTicksY;
            double y = valueToScreenY(val);

            // Grid line
            p.setPen(QPen(gridColor, 1, Qt::SolidLine));
            p.drawLine(plotRect.left(), y, plotRect.right(), y);

            // Label
            p.setPen(q->palette().text().color());
            QString label = QString::number(val, 'f', (maxY - minY < 10) ? 2 : 1);
            p.drawText(QRect(0, y - 10, kMarginLeft - 5, 20), Qt::AlignRight | Qt::AlignVCenter, label);
        }

        // X Axis labels (subsampled if too many)
        int numFrames = sortedPoints.size();
        int step = (std::max)(1, numFrames / 10);
        for (int i = 0; i < numFrames; i += step)
        {
            QPointF pt = dataToScreen(i, minY);
            p.setPen(q->palette().text().color());
            p.drawText(QRect(pt.x() - 20, plotRect.bottom() + 2, 40, 20), Qt::AlignCenter, QString::number(i + 1));
        }

        // Axis labels (bold)
        QFont titleFont = p.font();
        titleFont.setBold(true);
        p.setFont(titleFont);

        // X-axis label (varies by sort mode)
        QString xAxisLabel;
        switch (currentSortMode)
        {
        case SortMode::ByIndex:
            xAxisLabel = q->tr("Frame Index");
            break;
        case SortMode::ByValueAsc:
            xAxisLabel = q->tr("Rank (Ascending)");
            break;
        case SortMode::ByValueDesc:
            xAxisLabel = q->tr("Rank (Descending)");
            break;
        case SortMode::ByCaptureTime:
            xAxisLabel = q->tr("Capture Order");
            break;
        }
        p.drawText(QRect(plotRect.left(), plotRect.bottom() + 18, plotRect.width(), 20), Qt::AlignCenter, xAxisLabel);

        // Y-axis label (rotated)
        p.save();
        p.translate(12, plotRect.center().y());
        p.rotate(-90);
        p.drawText(QRect(-plotRect.height() / 2, -10, plotRect.height(), 20), Qt::AlignCenter, metricName());
        p.restore();

        // Restore antialiasing for subsequent paint calls.
        p.setRenderHint(QPainter::Antialiasing, true);
    }

    void paintMedianLines(QPainter& p)
    {
        if (sortedPoints.isEmpty())
            return;

        // Group points by groupKey
        QHash<QString, std::vector<double>> groupValues;
        for (const auto* pt : sortedPoints)
        {
            groupValues[pt->groupKey].push_back(getValue(*pt));
        }

        for (auto it = groupValues.begin(); it != groupValues.end(); ++it)
        {
            std::vector<double>& vals = it.value();
            if (vals.empty())
                continue;

            size_t n = vals.size();
            size_t mid = n / 2;
            std::nth_element(vals.begin(), vals.begin() + mid, vals.end());
            double median = vals[mid];
            if (n % 2 == 0 && mid > 0)
            {
                // For even-sized arrays, average the two middle values.
                double leftOfMid = *std::max_element(vals.begin(), vals.begin() + mid);
                median = (leftOfMid + median) / 2.0;
            }

            double y = valueToScreenY(median);
            QColor color = getGroupColor(it.key());

            p.setPen(QPen(color, 1, Qt::DashLine));
            p.drawLine(plotRect.left(), y, plotRect.right(), y);
        }
    }

    void paintAreaForGroup(QPainter& p, const QString& groupKey)
    {
        QColor fillColor = getGroupColor(groupKey);
        QColor strokeColor = fillColor;
        fillColor.setAlpha(60);

        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(fillColor);
        p.setPen(QPen(strokeColor, 1));

        QPainterPath path;
        bool hasAny = false;
        int prevIndex = -999999;

        auto flushSegment = [&]() {
            if (!hasAny)
                return;
            path.lineTo(path.currentPosition().x(), plotRect.bottom());
            path.closeSubpath();
            p.drawPath(path);
            path = QPainterPath();
            hasAny = false;
        };

        for (int i = 0; i < sortedPoints.size(); ++i)
        {
            if (sortedPoints[i]->groupKey != groupKey)
                continue;

            QPointF pt = dataToScreen(i, getValue(*sortedPoints[i]));

            if (!hasAny || i != (prevIndex + 1))
            {
                // New segment (gap in X)
                flushSegment();
                path.moveTo(pt.x(), plotRect.bottom());
                path.lineTo(pt);
                hasAny = true;
            }
            else
            {
                path.lineTo(pt);
            }

            prevIndex = i;
        }

        flushSegment();
    }

    void paintLegend(QPainter& p)
    {
        // Always show marker legend, group legend only when multiple groups
        const QRect bounds = (legendDockedRight && legendAreaRect.isValid()) ? legendAreaRect : plotRect;

        int x = bounds.right() - 10;
        int y = bounds.top() + 10;
        int rowHeight = 16;

        QFont font = an::ui::tokens().typography.caption;
        p.setFont(font);

        // Check what special markers exist in the data
        bool hasReference =
            std::any_of(allPoints.begin(), allPoints.end(), [](const ChartDataPoint& pt) { return pt.isReference; });
        bool hasExcluded =
            std::any_of(allPoints.begin(), allPoints.end(), [](const ChartDataPoint& pt) { return pt.isExcluded; });
        bool hasThreshold = (currentMetric == QualityMetric::Stars && minStarsThreshold > 0) ||
                            (currentMetric == QualityMetric::FWHM && maxFwhmThreshold > 0.0) ||
                            (currentMetric == QualityMetric::Eccentricity && maxEccThreshold > 0.0);

        // Calculate legend width based on longest text
        QStringList legendItems;
        if (showPercentileBand && stats.valid)
            legendItems << QObject::tr("▒ Q1–Q3");
        if (showMedianLines)
            legendItems << QObject::tr("┄ Median");
        if (hasReference)
            legendItems << QObject::tr("★ Reference");
        if (hasExcluded)
            legendItems << QObject::tr("✕ Excluded");
        if (hasThreshold)
            legendItems << QObject::tr("─ ─ Threshold");

        for (const auto& g : groups)
        {
            if (groups.size() > 1)
            {
                legendItems << g.displayName;
            }
        }

        if (legendItems.isEmpty())
            return;

        int maxTextWidth = 0;
        for (const auto& text : legendItems)
        {
            maxTextWidth = (std::max)(maxTextWidth, p.fontMetrics().horizontalAdvance(text));
        }
        int totalWidth = maxTextWidth + 24;

        // Clamp legend width when docked, to keep the plot usable and avoid drawing outside.
        if (legendDockedRight && bounds.isValid())
        {
            const int maxBgWidth = (std::max)(80, bounds.width() - 8);
            const int desiredBgWidth = totalWidth + 12;
            const int bgWidth = (std::min)(desiredBgWidth, maxBgWidth);
            totalWidth = (std::max)(0, bgWidth - 12);
        }

        // Draw legend background
        int legendHeight = legendItems.size() * rowHeight + 8;
        QRect bgRect(x - totalWidth - 8, y - 4, totalWidth + 12, legendHeight);

        // Ensure the legend box stays within the available bounds.
        if (bgRect.left() < bounds.left() + 2)
        {
            bgRect.moveLeft(bounds.left() + 2);
        }
        if (bgRect.right() > bounds.right() - 2)
        {
            bgRect.moveRight(bounds.right() - 2);
        }

        p.setBrush(q->palette().base());
        p.setPen(q->palette().color(QPalette::Mid));
        p.drawRect(bgRect);

        y += 2;

        // When docked, ensure long legend text doesn't spill into the plot.
        const int maxTextPx = (std::max)(0, bgRect.width() - 28);
        const auto elide = [&](const QString& text) -> QString {
            return p.fontMetrics().elidedText(text, Qt::ElideRight, maxTextPx);
        };

        // Draw overlays legend
        if (showPercentileBand && stats.valid)
        {
            QColor bandColor = q->palette().highlight().color();
            bandColor.setAlpha(60);
            p.fillRect(QRect(x - totalWidth, y + 2, 12, 10), bandColor);
            p.setPen(q->palette().text().color());
            p.drawText(x - totalWidth + 16, y + 10, elide(QObject::tr("Q1–Q3")));
            y += rowHeight;
        }

        if (showMedianLines)
        {
            QColor medianColor = (groups.size() == 1) ? getGroupColor(groups[0].key) : q->palette().text().color();
            p.setPen(QPen(medianColor, 1, Qt::DashLine));
            p.drawLine(x - totalWidth + 2, y + 6, x - totalWidth + 12, y + 6);
            p.setPen(q->palette().text().color());
            p.drawText(x - totalWidth + 16, y + 10, elide(QObject::tr("Median")));
            y += rowHeight;
        }

        // Draw special markers legend
        if (hasReference)
        {
            p.setBrush(an::ui::tokens().colors.statusWarning);
            p.setPen(an::ui::tokens().colors.textPrimary);
            drawStar(p, QPointF(x - totalWidth + 6, y + 6), 6);
            p.setPen(q->palette().text().color());
            p.drawText(x - totalWidth + 16, y + 10, elide(QObject::tr("Reference")));
            y += rowHeight;
        }

        if (hasExcluded)
        {
            p.setPen(QPen(an::ui::tokens().colors.statusError, 2));
            p.drawLine(QPointF(x - totalWidth + 2, y + 2), QPointF(x - totalWidth + 10, y + 10));
            p.drawLine(QPointF(x - totalWidth + 2, y + 10), QPointF(x - totalWidth + 10, y + 2));
            p.setPen(q->palette().text().color());
            p.drawText(x - totalWidth + 16, y + 10, elide(QObject::tr("Excluded")));
            y += rowHeight;
        }

        if (hasThreshold)
        {
            p.setPen(QPen(an::ui::tokens().colors.statusWarning, 2, Qt::DashLine));
            p.drawLine(x - totalWidth + 2, y + 6, x - totalWidth + 12, y + 6);
            p.setPen(q->palette().text().color());
            p.drawText(x - totalWidth + 16, y + 10, elide(QObject::tr("Threshold")));
            y += rowHeight;
        }

        // Draw group swatches (only for multiple groups)
        if (groups.size() > 1)
        {
            for (const auto& g : groups)
            {
                // Draw swatch
                QRect swatchRect(x - totalWidth, y + 1, 12, 12);
                p.setBrush(g.color);
                p.setPen(Qt::NoPen);
                p.drawRect(swatchRect);

                // Draw Text
                p.setPen(q->palette().text().color());
                p.drawText(x - totalWidth + 16, y + 10, elide(g.displayName));

                y += rowHeight;
            }
        }
    }

    void paintThresholdLines(QPainter& p)
    {
        double thresholdVal = -1.0;
        bool isMin = false;

        if (currentMetric == QualityMetric::Score && minScoreThreshold > 0.0)
        {
            thresholdVal = minScoreThreshold;
            isMin = true;
        }
        else if (currentMetric == QualityMetric::Stars && minStarsThreshold > 0)
        {
            thresholdVal = static_cast<double>(minStarsThreshold);
            isMin = true;
        }
        else if (currentMetric == QualityMetric::SNR && minSnrThreshold > 0.0)
        {
            thresholdVal = minSnrThreshold;
            isMin = true;
        }
        else if (currentMetric == QualityMetric::FWHM && maxFwhmThreshold > 0.0)
        {
            thresholdVal = maxFwhmThreshold;
        }
        else if (currentMetric == QualityMetric::Eccentricity && maxEccThreshold > 0.0)
        {
            thresholdVal = maxEccThreshold;
        }

        if (thresholdVal >= 0)
        {
            double y = valueToScreenY(thresholdVal);
            // Only draw if within reasonable bounds (with some margin)
            if (y >= plotRect.top() - 10 && y <= plotRect.bottom() + 10)
            {
                const QColor warnColor = an::ui::tokens().colors.statusWarning;
                p.setPen(QPen(warnColor, 2, Qt::DashLine));
                p.drawLine(plotRect.left(), y, plotRect.right(), y);

                // Draw Label
                p.setPen(warnColor);
                QString label = QString("%1 %2").arg(isMin ? ">=" : "<=").arg(thresholdVal, 0, 'f', 2);
                p.drawText(plotRect.left() + 5, y - 4, label);
            }
        }
    }

    void paintPercentileBand(QPainter& p)
    {
        if (!stats.valid)
            return;

        double yQ1 = valueToScreenY(stats.q1);
        double yQ3 = valueToScreenY(stats.q3);

        // Ensure Q3 is above Q1 in value, but screen Y is inverted
        double top = (std::min)(yQ1, yQ3);
        double bottom = (std::max)(yQ1, yQ3);

        QRectF bandRect(plotRect.left(), top, plotRect.width(), bottom - top);

        // Clamp to plot rect
        bandRect = bandRect.intersected(plotRect);

        // Use highlight color for band, very transparent
        QColor bandColor = q->palette().highlight().color();
        bandColor.setAlpha(30);

        p.fillRect(bandRect, bandColor);
    }

    void paintBars(QPainter& p)
    {
        if (sortedPoints.isEmpty())
            return;

        double barWidth = (plotRect.width() / (double)sortedPoints.size()) * 0.8;
        if (barWidth < 1)
            barWidth = 1;

        p.setPen(Qt::NoPen);

        for (int i = 0; i < sortedPoints.size(); ++i)
        {
            const auto* dp = sortedPoints[i];
            double val = getValue(*dp);
            QPointF topPt = dataToScreen(i, val);
            double baseY = valueToScreenY((std::max)(0.0, minY)); // Base at 0 or min

            QRectF barRect(topPt.x() - barWidth / 2, topPt.y(), barWidth, baseY - topPt.y());

            QColor color = getGroupColor(dp->groupKey);
            if (dp->failsThreshold)
                color = an::ui::tokens().colors.statusWarning;
            if (dp->isExcluded)
                color.setAlpha(100);
            if (dp->frameId == selectedFrameId)
                color = q->palette().highlight().color();

            p.setBrush(color);
            p.drawRect(barRect);
        }
    }

    void paintStatisticsPanel(QPainter& p)
    {
        if (!stats.valid)
            return;

        QRect panelRect = q->rect();
        panelRect.setTop(plotRect.bottom() + 5);

        QStringList statsText;
        statsText << QString("Min: %1").arg(stats.min, 0, 'f', 2);
        statsText << QString("Median: %1").arg(stats.median, 0, 'f', 2);
        statsText << QString("Mean: %1").arg(stats.mean, 0, 'f', 2);
        statsText << QString("Max: %1").arg(stats.max, 0, 'f', 2);
        statsText << QString("σ: %1").arg(stats.stdDev, 0, 'f', 2);

        int total = allPoints.size();
        int excluded = 0;
        int references = 0;
        for (const auto& pt : allPoints)
        {
            if (pt.isExcluded)
                excluded++;
            if (pt.isReference)
                references++;
        }

        QString framesText = QString("Frames: %1 total").arg(total);
        if (excluded > 0)
            framesText += QString(", %1 excluded").arg(excluded);
        if (references > 0)
            framesText += QString(", %1 reference%2").arg(references).arg(references > 1 ? "s" : "");

        // Show selected value if any
        if (!selectedFrameId.isEmpty())
        {
            // Find selected point value
            auto it = std::find_if(sortedPoints.begin(), sortedPoints.end(),
                                   [&](const ChartDataPoint* pt) { return pt->frameId == selectedFrameId; });
            if (it != sortedPoints.end())
            {
                framesText += QString("  [Selected: %1]").arg(getValue(**it), 0, 'f', 2);
            }
        }

        p.setPen(q->palette().text().color());
        QFont font = p.font();
        // font.setPointSize(font.pointSize()); // Keep default size
        p.setFont(font);

        // Draw left aligned stats
        p.drawText(panelRect.adjusted(kMarginLeft, 0, -kMarginRight, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   statsText.join("  "));

        // Draw right aligned frame info
        p.drawText(panelRect.adjusted(kMarginLeft, 0, -kMarginRight, 0), Qt::AlignRight | Qt::AlignVCenter, framesText);
    }

    void drawStar(QPainter& p, const QPointF& center, double size)
    {
        // Simple 5-point star
        QPolygonF star;
        for (int i = 0; i < 5; ++i)
        {
            double angle = i * 4 * M_PI / 5 - M_PI / 2;
            star << center + QPointF(std::cos(angle) * size, std::sin(angle) * size);
        }
        p.drawPolygon(star);
    }

    int indexAtPos(const QPoint& pos) const
    {
        if (sortedPoints.isEmpty() || !plotRect.contains(pos))
            return -1;

        double xStep = (plotRect.width()) / (double)(std::max)(1, static_cast<int>(sortedPoints.size()) - 1);
        int idx = std::round((pos.x() - plotRect.left()) / xStep);

        if (idx >= 0 && idx < sortedPoints.size())
        {
            // Check distance to actual point to avoid false positives in empty space
            QPointF pt = dataToScreen(idx, getValue(*sortedPoints[idx]));
            if (std::abs(pt.x() - pos.x()) < xStep / 2 + 5)
            { // Tolerance
                return idx;
            }
        }
        return -1;
    }
};

QualityDistributionWidget::QualityDistributionWidget(QWidget* parent)
    : QWidget(parent), m_impl(std::make_unique<Impl>(this))
{
    m_impl->setupUi();
    m_impl->updateSeriesUiAvailability();
    setMouseTracking(true); // For hover effects
    setFocusPolicy(Qt::StrongFocus); // For keyboard navigation
}

QualityDistributionWidget::~QualityDistributionWidget() = default;

QSize QualityDistributionWidget::sizeHint() const
{
    return QSize(600, 260);
}

QSize QualityDistributionWidget::minimumSizeHint() const
{
    return QSize(240, 140);
}

void QualityDistributionWidget::setData(const QVector<ChartDataPoint>& points, const QVector<ChartGroupInfo>& groups)
{
    m_impl->sourcePoints = points;
    m_impl->sourceGroups = groups;
    m_impl->updateSeriesUiAvailability();
    m_impl->rebuildSeriesData();
    m_impl->updateThresholdStatus();
    m_impl->resortData();
    update();
}

void QualityDistributionWidget::clearData()
{
    m_impl->sourcePoints.clear();
    m_impl->sourceGroups.clear();
    m_impl->allPoints.clear();
    m_impl->groups.clear();
    m_impl->sortedPoints.clear();
    update();
}

bool QualityDistributionWidget::hasData() const
{
    return !m_impl->allPoints.isEmpty();
}

void QualityDistributionWidget::setMetric(QualityMetric metric)
{
    if (m_impl->currentMetric != metric)
    {
        m_impl->currentMetric = metric;
        // Update combo box if changed programmatically
        int idx = m_impl->comboMetric->findData(QVariant::fromValue(metric));
        if (idx >= 0)
            m_impl->comboMetric->setCurrentIndex(idx);

        m_impl->resortData();
        update();
    }
}

QualityDistributionWidget::QualityMetric QualityDistributionWidget::metric() const
{
    return m_impl->currentMetric;
}

void QualityDistributionWidget::setPlotStyle(PlotStyle style)
{
    m_impl->currentPlotStyle = style;
    int idx = m_impl->comboPlotStyle->findData(QVariant::fromValue(style));
    if (idx >= 0)
        m_impl->comboPlotStyle->setCurrentIndex(idx);
    update();
}

QualityDistributionWidget::PlotStyle QualityDistributionWidget::plotStyle() const
{
    return m_impl->currentPlotStyle;
}

void QualityDistributionWidget::setSortMode(SortMode mode)
{
    m_impl->currentSortMode = mode;
    int idx = m_impl->comboSort->findData(QVariant::fromValue(mode));
    if (idx >= 0)
        m_impl->comboSort->setCurrentIndex(idx);
    m_impl->resortData();
    update();
}

QualityDistributionWidget::SortMode QualityDistributionWidget::sortMode() const
{
    return m_impl->currentSortMode;
}

void QualityDistributionWidget::setNormalizeMode(NormalizeMode mode)
{
    if (m_impl->currentNormalizeMode != mode)
    {
        m_impl->currentNormalizeMode = mode;
        int idx = m_impl->comboNormalize->findData(QVariant::fromValue(mode));
        if (idx >= 0)
            m_impl->comboNormalize->setCurrentIndex(idx);
        m_impl->resortData();
        update();
    }
}

QualityDistributionWidget::NormalizeMode QualityDistributionWidget::normalizeMode() const
{
    return m_impl->currentNormalizeMode;
}

void QualityDistributionWidget::setShowPercentileBand(bool show)
{
    m_impl->showPercentileBand = show;
    if (m_impl->checkPercentileBand)
    {
        m_impl->checkPercentileBand->setChecked(show);
    }
    update();
}

bool QualityDistributionWidget::showPercentileBand() const
{
    return m_impl->showPercentileBand;
}

void QualityDistributionWidget::setMinScoreThreshold(double threshold)
{
    m_impl->minScoreThreshold = threshold;
    m_impl->updateThresholdStatus();
    update();
}

void QualityDistributionWidget::setMinStarsThreshold(int threshold)
{
    m_impl->minStarsThreshold = threshold;
    m_impl->updateThresholdStatus();
    update();
}

void QualityDistributionWidget::setMinSnrThreshold(double threshold)
{
    m_impl->minSnrThreshold = threshold;
    m_impl->updateThresholdStatus();
    update();
}

void QualityDistributionWidget::setMaxFwhmThreshold(double threshold)
{
    m_impl->maxFwhmThreshold = threshold;
    m_impl->updateThresholdStatus();
    update();
}

void QualityDistributionWidget::setMaxEccentricityThreshold(double threshold)
{
    m_impl->maxEccThreshold = threshold;
    m_impl->updateThresholdStatus();
    update();
}

void QualityDistributionWidget::clearThresholds()
{
    m_impl->minScoreThreshold = 0.0;
    m_impl->minStarsThreshold = 0;
    m_impl->minSnrThreshold = 0.0;
    m_impl->maxFwhmThreshold = 0.0;
    m_impl->maxEccThreshold = 0.0;
    m_impl->updateThresholdStatus();
    update();
}

void QualityDistributionWidget::setSelectedFrameId(const QString& frameId)
{
    if (m_impl->selectedFrameId != frameId)
    {
        m_impl->selectedFrameId = frameId;
        update();
    }
}

void QualityDistributionWidget::clearSelection()
{
    if (!m_impl->selectedFrameId.isEmpty())
    {
        m_impl->selectedFrameId.clear();
        update();
    }
}

QString QualityDistributionWidget::selectedFrameId() const
{
    return m_impl->selectedFrameId;
}

void QualityDistributionWidget::paintEvent(QPaintEvent* event)
{
    QPainter p(this);
    m_impl->paint(p);
}

void QualityDistributionWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
}

void QualityDistributionWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        int idx = m_impl->indexAtPos(event->pos());
        if (idx >= 0)
        {
            const auto* pt = m_impl->sortedPoints[idx];
            setSelectedFrameId(pt->frameId);
            emit frameClicked(pt->frameId);
        }
        else
        {
            // clearSelection(); // Optional: deselect on click empty space
        }
    }
    else if (event->button() == Qt::RightButton)
    {
        // Context menu handled in contextMenuEvent
    }
}

void QualityDistributionWidget::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);
}

void QualityDistributionWidget::mouseMoveEvent(QMouseEvent* event)
{
    int oldHover = m_impl->hoveredPointIndex;
    m_impl->hoveredPointIndex = m_impl->indexAtPos(event->pos());

    if (m_impl->hoveredPointIndex != oldHover)
    {
        update();
    }

    if (m_impl->hoveredPointIndex >= 0)
    {
        const auto* pt = m_impl->sortedPoints[m_impl->hoveredPointIndex];
        QToolTip::showText(event->globalPosition().toPoint(), m_impl->buildTooltipHtml(*pt), this);
    }
    else
    {
        QToolTip::hideText();
    }
}

void QualityDistributionWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        int idx = m_impl->indexAtPos(event->pos());
        if (idx >= 0)
        {
            const auto* pt = m_impl->sortedPoints[idx];
            emit frameDoubleClicked(pt->frameId);
        }
    }
}

void QualityDistributionWidget::wheelEvent(QWheelEvent* event)
{
    QWidget::wheelEvent(event);
}

void QualityDistributionWidget::leaveEvent(QEvent* event)
{
    if (m_impl->hoveredPointIndex != -1)
    {
        m_impl->hoveredPointIndex = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

void QualityDistributionWidget::contextMenuEvent(QContextMenuEvent* event)
{
    int idx = m_impl->indexAtPos(event->pos());
    if (idx >= 0)
    {
        const auto* pt = m_impl->sortedPoints[idx];
        // Select it first
        setSelectedFrameId(pt->frameId);
        emit frameClicked(pt->frameId);
        emit frameContextMenuRequested(pt->frameId, event->globalPos());
        return;
    }

    // Background menu
    QMenu menu(this);

    QAction* copyAction = menu.addAction(tr("Copy Chart Image"));
    connect(copyAction, &QAction::triggered, this, &QualityDistributionWidget::copyChartImageToClipboard);

    QAction* saveImageAction = menu.addAction(tr("Save Chart as Image..."));
    connect(saveImageAction, &QAction::triggered, this, [this]() {
        const QPointer<QualityDistributionWidget> widget(this);
        selectChartExportPath(this, tr("Save Chart Image"), tr("Images (*.png *.jpg)"), [widget](const QString& path) {
            if (widget)
            {
                widget->exportChartImage(path);
            }
        });
    });

    QAction* exportCsvAction = menu.addAction(tr("Export Data to CSV..."));
    connect(exportCsvAction, &QAction::triggered, this, [this]() {
        const QPointer<QualityDistributionWidget> widget(this);
        selectChartExportPath(this, tr("Export CSV"), tr("CSV Files (*.csv)"), [widget](const QString& path) {
            if (widget)
            {
                widget->exportChartDataToCsv(path);
            }
        });
    });

    menu.exec(event->globalPos());
}

void QualityDistributionWidget::keyPressEvent(QKeyEvent* event)
{
    if (m_impl->sortedPoints.isEmpty())
    {
        QWidget::keyPressEvent(event);
        return;
    }

    int currentIndex = -1;
    if (!m_impl->selectedFrameId.isEmpty())
    {
        for (int i = 0; i < m_impl->sortedPoints.size(); ++i)
        {
            if (m_impl->sortedPoints[i]->frameId == m_impl->selectedFrameId)
            {
                currentIndex = i;
                break;
            }
        }
    }

    int nextIndex = currentIndex;

    switch (event->key())
    {
    case Qt::Key_Left:
    case Qt::Key_Up:
        if (currentIndex == -1)
            nextIndex = m_impl->sortedPoints.size() - 1;
        else
            nextIndex = (std::max)(0, currentIndex - 1);
        break;
    case Qt::Key_Right:
    case Qt::Key_Down:
        if (currentIndex == -1)
            nextIndex = 0;
        else
            nextIndex = (std::min)((int)m_impl->sortedPoints.size() - 1, currentIndex + 1);
        break;
    case Qt::Key_Home:
        nextIndex = 0;
        break;
    case Qt::Key_End:
        nextIndex = m_impl->sortedPoints.size() - 1;
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }

    if (nextIndex != -1 && nextIndex != currentIndex && nextIndex < m_impl->sortedPoints.size())
    {
        const auto* pt = m_impl->sortedPoints[nextIndex];
        setSelectedFrameId(pt->frameId);
        emit frameClicked(pt->frameId);
    }
}

bool QualityDistributionWidget::exportChartImage(const QString& filePath, const char* format)
{
    if (filePath.isEmpty())
        return false;

    QPixmap pixmap(size());
    render(&pixmap);
    return pixmap.save(filePath, format);
}

void QualityDistributionWidget::copyChartImageToClipboard()
{
    QPixmap pixmap(size());
    render(&pixmap);
    QApplication::clipboard()->setPixmap(pixmap);
}

bool QualityDistributionWidget::exportChartDataToCsv(const QString& filePath)
{
    if (filePath.isEmpty())
        return false;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);

    // Header
    out << "Frame,Group,Filter,Metric Value,Is Reference,Is Excluded,Fails Threshold\n";

    // Data
    for (const auto* pt : m_impl->sortedPoints)
    {
        // Escape frame path/name if it contains comma
        QString name = pt->displayName;
        if (name.contains(','))
            name = "\"" + name + "\"";

        out << name << "," << pt->groupKey << "," << pt->filterName << "," << m_impl->getValue(*pt) << ","
            << (pt->isReference ? "Yes" : "No") << "," << (pt->isExcluded ? "Yes" : "No") << ","
            << (pt->failsThreshold ? "Yes" : "No") << "\n";
    }

    return true;
}
