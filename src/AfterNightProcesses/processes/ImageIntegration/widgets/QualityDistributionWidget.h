// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QWidget>
#include <QVector>
#include <QString>
#include <QColor>
#include <QDateTime>
#include <memory>
#include <verdigris/wobjectdefs.h>

class QComboBox;
class QCheckBox;
class QLabel;

namespace ImageIntegration
{

/**
 * @brief Quality metric types that can be displayed on the chart.
 */
enum class QualityMetric
{
    Score, ///< Composite quality score (default)
    Stars, ///< Number of detected stars
    FWHM, ///< Full Width at Half Maximum (seeing)
    SNR, ///< Signal-to-Noise Ratio
    Eccentricity ///< Star eccentricity (roundness)
};

/**
 * @brief Plot rendering styles.
 */
enum class PlotStyle
{
    Points, ///< Scatter plot with points only
    Line, ///< Connected line without points
    LinePoints, ///< Line with points at data positions
    Area, ///< Filled area under the line
    Bars ///< Vertical bar chart
};

/**
 * @brief Data point sorting modes.
 */
enum class SortMode
{
    ByIndex, ///< Original file order / capture order
    ByValueAsc, ///< Sort by metric value (ascending)
    ByValueDesc, ///< Sort by metric value (descending)
    ByCaptureTime ///< By capture timestamp (shows quality drift over session)
};

/**
 * @brief Normalization mode for cross-filter comparison.
 */
enum class NormalizeMode
{
    Off, ///< Show absolute values
    ByFilter ///< Normalize relative to per-filter median (z-score like)
};

/**
 * @brief Data for a single frame in the chart.
 */
struct ChartDataPoint
{
    QString frameId; ///< Canonical path (unique identifier)
    QString displayName; ///< Short display name (filename)
    QString groupKey; ///< Registration group key
    QString filterName; ///< Filter name (L, Ha, R, G, B, etc.)
    QDateTime captureTime; ///< Capture timestamp (for time-based sorting)

    double score = 0.0;
    int stars = 0;
    double fwhm = 0.0;
    double snr = 0.0;
    double eccentricity = 0.0;

    bool isReference = false;
    bool isExcluded = false;
    bool failsThreshold = false; ///< Would be excluded by current threshold settings
    bool hasMetrics = false; ///< False if registration not yet run
};

/**
 * @brief Registration group metadata for legend.
 */
struct ChartGroupInfo
{
    QString key; ///< Group key string
    QString displayName; ///< Human-readable group name
    QColor color; ///< Assigned color for this group
    int frameCount = 0;
    int excludedCount = 0;
    bool hasReference = false;
};

} // namespace ImageIntegration

/**
 * @class QualityDistributionWidget
 * @brief Interactive chart widget showing quality metric distribution.
 *
 * Displays frame quality metrics as a configurable chart with support for:
 * - Multiple metric types (Score, FWHM, Stars, SNR, Eccentricity)
 * - Multiple plot styles (points, lines, area, bars)
 * - Multiple registration groups with color coding
 * - Interactive selection and highlighting
 * - Threshold visualization
 * - Statistical overlays
 */
class AFTERNIGHTPROCESSES_API QualityDistributionWidget : public QWidget
{
    W_OBJECT(QualityDistributionWidget)

public:
    using QualityMetric = ImageIntegration::QualityMetric;
    using PlotStyle = ImageIntegration::PlotStyle;
    using SortMode = ImageIntegration::SortMode;
    using NormalizeMode = ImageIntegration::NormalizeMode;
    using ChartDataPoint = ImageIntegration::ChartDataPoint;
    using ChartGroupInfo = ImageIntegration::ChartGroupInfo;

    explicit QualityDistributionWidget(QWidget* parent = nullptr);
    ~QualityDistributionWidget() override;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // === Data ===

    /**
     * @brief Set the data points to display.
     * @param points Vector of frame data points.
     * @param groups Vector of group info for legend.
     */
    void setData(const QVector<ChartDataPoint>& points, const QVector<ChartGroupInfo>& groups);

    /**
     * @brief Clear all data from the chart.
     */
    void clearData();

    /**
     * @brief Check if the chart has data to display.
     */
    [[nodiscard]] bool hasData() const;

    // === Configuration ===

    void setMetric(QualityMetric metric);
    [[nodiscard]] QualityMetric metric() const;

    void setPlotStyle(PlotStyle style);
    [[nodiscard]] PlotStyle plotStyle() const;

    void setSortMode(SortMode mode);
    [[nodiscard]] SortMode sortMode() const;

    void setNormalizeMode(NormalizeMode mode);
    [[nodiscard]] NormalizeMode normalizeMode() const;

    void setShowPercentileBand(bool show);
    [[nodiscard]] bool showPercentileBand() const;

    // === Thresholds (for visualization only) ===

    void setMinScoreThreshold(double threshold);
    void setMinStarsThreshold(int threshold);
    void setMinSnrThreshold(double threshold);
    void setMaxFwhmThreshold(double threshold);
    void setMaxEccentricityThreshold(double threshold);
    void clearThresholds();

    // === Selection ===

    void setSelectedFrameId(const QString& frameId);
    void clearSelection();
    [[nodiscard]] QString selectedFrameId() const;

    // === Export ===

    /**
     * @brief Exports the current chart view to an image file.
     * @param filePath The path to save the image to.
     * @param format The image format (e.g., "PNG", "JPG"). If null, inferred from extension.
     * @return True if successful.
     */
    bool exportChartImage(const QString& filePath, const char* format = nullptr);

    /**
     * @brief Copies the current chart view to the system clipboard.
     */
    void copyChartImageToClipboard();

    /**
     * @brief Exports the current chart data to a CSV file.
     * @param filePath The path to save the CSV file to.
     * @return True if successful.
     */
    bool exportChartDataToCsv(const QString& filePath);

    // === Signals ===

    /**
     * @brief Emitted when user clicks on a data point.
     * @param frameId The canonical path of the clicked frame.
     */
    void frameClicked(const QString& frameId) W_SIGNAL(frameClicked, frameId);

    /**
     * @brief Emitted when user double-clicks on a data point.
     * @param frameId The canonical path of the double-clicked frame.
     */
    void frameDoubleClicked(const QString& frameId) W_SIGNAL(frameDoubleClicked, frameId);

    /**
     * @brief Emitted when user requests context menu on a data point.
     * @param frameId The canonical path of the frame.
     * @param globalPos The global position for the menu.
     */
    void frameContextMenuRequested(const QString& frameId, const QPoint& globalPos)
        W_SIGNAL(frameContextMenuRequested, frameId, globalPos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
