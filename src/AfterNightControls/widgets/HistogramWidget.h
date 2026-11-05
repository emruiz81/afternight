// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QWidget>
#include <QScrollArea>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "widgets/HistogramDisplayData.h"

class HistogramOverlay; // forward declaration

namespace an::ui::detail
{

[[nodiscard]] inline double
getMaxDisplayedHistogramValue(const std::shared_ptr<const an::ui::HistogramDisplayData>& histogram,
                              const std::array<bool, 4>& displayChannels)
{
    if (!histogram)
    {
        return 0.0;
    }

    double maxVal = 0.0;
    for (size_t i = 0; i < histogram->channels.size(); ++i)
    {
        if (i < displayChannels.size() && !displayChannels[i])
        {
            continue;
        }

        maxVal = (std::max)(maxVal, static_cast<double>(histogram->channels[i].maxValue()));
    }
    return maxVal;
}

} // namespace an::ui::detail

// Draws the counts its owner hands it. The widget owns no image and counts nothing: whoever owns the image counts it
// through widgets/HistogramComputation.h, on a worker, at the resolution the widget names, and hands the result over.
class AFTERNIGHTCONTROLS_API HistogramWidget : public QWidget
{
public:
    HistogramWidget(QWidget* parent);

    // The resolution the owner counts at; setting it counts nothing.
    void setHistogramResolution(an::ui::HistogramResolution resolution);
    an::ui::HistogramResolution getHistogramResolution() const;

    // The counts drawn; null draws nothing. Called off the GUI thread, it is queued to the GUI thread rather than
    // waited for.
    void setHistogram(std::shared_ptr<const an::ui::HistogramDisplayData> histogram);
    std::shared_ptr<const an::ui::HistogramDisplayData> getHistogram() const;

    void setGridEnabled(bool enabled);
    bool getGridEnabled() const;

    void setGridSize(const size_t gridSizeW, const size_t gridSizeH);
    void setGridWidth(const float w);
    float getGridWidth() const;

    void setLocalChannelNormalization(bool enabled);
    bool getLocalChannelNormalization() const;

    void setClippingInfoEnabled(bool enabled);
    bool getClippingInfoEnabled() const;

    void setGradientAreaEnabled(bool enabled);
    bool getGradientAreaEnabled() const;

    void setGuideLineEnabled(bool enabled);
    bool getGuideLineEnabled() const;

    void zoomIn();

    void zoomOut();

    void resetZoom();

    void resetDisplayChannels();

    void setDisplayChannels(bool red, bool green, bool blue, bool aux1 = false);

    [[nodiscard]] double getMaxDisplayedHistogramValue() const;

    void setInteractiveZoomEnabled(bool enabled);

    bool getInteractiveZoomEnabled() const;

    void setParentScrollArea(QScrollArea* area);
    QScrollArea* getParentScrollArea() const;

    void setAreaModeEnabled(bool enabled);
    bool getAreaModeEnabled() const;
    void setAreaOpacity(float opacity);
    float getAreaOpacity() const;

    [[nodiscard]] QColor getGridColor() const;
    [[nodiscard]] QColor getBackgroundColor() const;
    [[nodiscard]] QColor getGuideLineColor() const;
    [[nodiscard]] bool isValid() const
    {
        return true;
    }

    ~HistogramWidget();

protected:
    struct histogram_render_job
    {
        std::shared_ptr<std::vector<double>> histogram_data = std::make_shared<std::vector<double>>();
        double maxVal = 0.0;
    };

    void paintEvent(QPaintEvent* evt) override;
    void resizeEvent(QResizeEvent* evt) override;
    void wheelEvent(QWheelEvent* ev) override;
    void changeEvent(QEvent* ev) override;

    void mousePressEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;

    void drawBackground(QPainter& painter);
    void drawGrid(QPainter& painter);

    void drawHistogram(QPainter& painter);
    void drawHistogramChannel(QPainter& painter, const an::ui::HistogramDisplayChannel& channel, size_t globalMaxValue);
    void drawPreparedHistogram(QPainter& painter, size_t channelIdx, double globalMaxValue);
    // New helpers for area drawing
    void drawHistogramChannelArea(QPainter& painter, const an::ui::HistogramDisplayChannel& channel,
                                  size_t globalMaxValue, const QColor& color);
    void drawPreparedHistogramArea(QPainter& painter, size_t channelIdx, double globalMaxValue, const QColor& color);
    void drawGuideLine(QPainter& painter);
    void drawGradientArea(QPainter& painter);

    void prepareHistogramsToRender();
    void updateClippingInfo();

    double getMaxGlobalValue() const;
    double getMaxDisplayedGlobalValue() const; // new

    virtual int getPlotHeight();

    void beginCoordsTooltip(const QPoint& localPos);
    void updateCoordsTooltip(const QPoint& localPos);
    void endCoordsTooltip();

    bool m_GridEnabled = true;
    bool m_LocalChannelNormalization = false;

    std::shared_ptr<const an::ui::HistogramDisplayData> m_Histogram = std::make_shared<an::ui::HistogramDisplayData>();
    an::ui::HistogramResolution m_HistogramResolution = an::ui::HistogramResolution::Bits8;

    std::array<bool, 4> m_DisplayChannels = {true, true, true, false};

    size_t m_NumOfGridsW = 8;
    size_t m_NumOfGridsH = 8;
    float m_GridWidth = 0.5f;

    bool m_ClippingInfoEnabled = false;

    bool m_GradientAreaEnabled = true;
    QRect m_GradientArea;

    bool m_GuideLineEnabled = false;

    bool m_InteractiveZoomEnabled = false;

    QScrollArea* m_ParentScrollArea = nullptr;

    QPoint m_LastMouseDragLocation = QPoint(0, 0);
    bool m_MouseLeftButtonPressed = false;
    bool m_MouseMiddleButtonPressed = false;

    std::vector<histogram_render_job> m_HistogramsToRender;

    // New state for area drawing
    bool m_DrawAreasEnabled = false;
    float m_AreaOpacity = 0.6f; // 0..1

private:
    [[nodiscard]] int currentRenderWidth() const;
    // Centralized helper to build and show the tooltip at the given local position
    void showTooltipAt(const QPoint& localPos);
    void refreshOverlayColors();

    // Dedicated overlay widget for coordinates
    HistogramOverlay* m_CoordsOverlay = nullptr;
    HistogramOverlay* m_ClippingOverlay = nullptr;
    std::atomic_int m_RenderWidth = 0;
};
