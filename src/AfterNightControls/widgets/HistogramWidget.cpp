// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "HistogramWidget.h"
#include "app/GuiThreadInvoke.h"
#include "theme/AnDesignTokens.h"
#include "theme/NightImage.h"

#include "HistogramOverlay.h"

#include <QPointer>

constexpr int GRADIENT_AREA_HEIGHT = 12;

namespace
{
QColor withAlpha(const QColor& color, const int alpha)
{
    return QColor(color.red(), color.green(), color.blue(), alpha);
}
} // namespace

HistogramWidget::HistogramWidget(QWidget* parent) : QWidget(parent)
{
    this->setAttribute(Qt::WA_DeleteOnClose);
    m_RenderWidth.store(width(), std::memory_order_relaxed);

    m_ClippingOverlay = new HistogramOverlay(this);
    m_ClippingOverlay->setRoundedCorners(false);
    m_ClippingOverlay->move(QPoint(5, 5));
    refreshOverlayColors();
}

void HistogramWidget::setHistogramResolution(const an::ui::HistogramResolution resolution)
{
    m_HistogramResolution = resolution;
}

an::ui::HistogramResolution HistogramWidget::getHistogramResolution() const
{
    return m_HistogramResolution;
}

void HistogramWidget::setHistogram(std::shared_ptr<const an::ui::HistogramDisplayData> histogram)
{
    if (QThread::currentThread() != thread())
    {
        // The counts the paint reads are the GUI thread's: a worker's are handed over, never waited for.
        QPointer<HistogramWidget> guard(this);
        QMetaObject::invokeMethod(
            this,
            [guard, histogram = std::move(histogram)]() mutable {
                if (guard)
                {
                    guard->setHistogram(std::move(histogram));
                }
            },
            Qt::QueuedConnection);
        return;
    }
    m_Histogram = histogram ? std::move(histogram) : std::make_shared<an::ui::HistogramDisplayData>();
    prepareHistogramsToRender();
    updateClippingInfo();
    update();
}

std::shared_ptr<const an::ui::HistogramDisplayData> HistogramWidget::getHistogram() const
{
    return m_Histogram;
}

void HistogramWidget::setGridEnabled(bool enabled)
{
    m_GridEnabled = enabled;
    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

bool HistogramWidget::getGridEnabled() const
{
    return m_GridEnabled;
}

void HistogramWidget::setGridSize(const size_t gridSizeW, const size_t gridSizeH)
{
    m_NumOfGridsW = gridSizeW;
    m_NumOfGridsH = gridSizeH;

    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

void HistogramWidget::setGridWidth(const float w)
{
    m_GridWidth = w;
}

float HistogramWidget::getGridWidth() const
{
    return m_GridWidth;
}

void HistogramWidget::setLocalChannelNormalization(bool enabled)
{
    m_LocalChannelNormalization = enabled;

    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

bool HistogramWidget::getLocalChannelNormalization() const
{
    return m_LocalChannelNormalization;
}

void HistogramWidget::setClippingInfoEnabled(bool enabled)
{
    m_ClippingInfoEnabled = enabled;
    if (m_ClippingInfoEnabled)
    {
        m_ClippingOverlay->show();
    }
    else
    {
        m_ClippingOverlay->hide();
    }

    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

bool HistogramWidget::getClippingInfoEnabled() const
{
    return m_ClippingInfoEnabled;
}

void HistogramWidget::setGradientAreaEnabled(bool enabled)
{
    m_GradientAreaEnabled = enabled;
    if (m_GradientAreaEnabled)
    {
        m_GradientArea.setX(0);
        m_GradientArea.setWidth(width());
        m_GradientArea.setY(height() - GRADIENT_AREA_HEIGHT);
        m_GradientArea.setHeight(GRADIENT_AREA_HEIGHT);
    }

    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

bool HistogramWidget::getGradientAreaEnabled() const
{
    return m_GradientAreaEnabled;
}

void HistogramWidget::setGuideLineEnabled(bool enabled)
{
    m_GuideLineEnabled = enabled;
    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

bool HistogramWidget::getGuideLineEnabled() const
{
    return m_GuideLineEnabled;
}

// New API implementations
void HistogramWidget::setAreaModeEnabled(bool enabled)
{
    m_DrawAreasEnabled = enabled;
    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

bool HistogramWidget::getAreaModeEnabled() const
{
    return m_DrawAreasEnabled;
}

void HistogramWidget::setAreaOpacity(float opacity)
{
    m_AreaOpacity = std::clamp(opacity, 0.0f, 1.0f);
    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

float HistogramWidget::getAreaOpacity() const
{
    return m_AreaOpacity;
}

HistogramWidget::~HistogramWidget() {}

QColor HistogramWidget::getGridColor() const
{
    return an::ui::tokens().colors.borderPrimary;
}

QColor HistogramWidget::getBackgroundColor() const
{
    // A plot is drawn in a well, dark in both themes, so that its curves keep their colours whatever the theme.
    return an::ui::tokens().colors.bgWell;
}

QColor HistogramWidget::getGuideLineColor() const
{
    return an::ui::tokens().colors.textPrimary;
}

void HistogramWidget::changeEvent(QEvent* ev)
{
    if (ev->type() == QEvent::ApplicationPaletteChange || ev->type() == QEvent::ThemeChange)
    {
        refreshOverlayColors();
        update();
    }
    QWidget::changeEvent(ev);
}

void HistogramWidget::resetDisplayChannels()
{
    m_DisplayChannels[0] = true;
    m_DisplayChannels[1] = true;
    m_DisplayChannels[2] = true;
    m_DisplayChannels[3] = false;

    prepareHistogramsToRender();
    updateClippingInfo();
    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

void HistogramWidget::setDisplayChannels(bool red, bool green, bool blue, bool aux1)
{
    m_DisplayChannels[0] = red;
    m_DisplayChannels[1] = green;
    m_DisplayChannels[2] = blue;
    m_DisplayChannels[3] = aux1;

    prepareHistogramsToRender();
    updateClippingInfo();
    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

double HistogramWidget::getMaxDisplayedHistogramValue() const
{
    return an::ui::detail::getMaxDisplayedHistogramValue(m_Histogram, m_DisplayChannels);
}

void HistogramWidget::setInteractiveZoomEnabled(bool enabled)
{
    m_InteractiveZoomEnabled = enabled;
}

bool HistogramWidget::getInteractiveZoomEnabled() const
{
    return m_InteractiveZoomEnabled;
}

void HistogramWidget::setParentScrollArea(QScrollArea* area)
{
    m_ParentScrollArea = area;
}

QScrollArea* HistogramWidget::getParentScrollArea() const
{
    return m_ParentScrollArea;
}

void HistogramWidget::paintEvent(QPaintEvent* evt)
{
    evt->accept();

    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, true);

    drawBackground(painter);

    if (m_GridEnabled)
        drawGrid(painter);

    if (m_GradientAreaEnabled)
        drawGradientArea(painter);

    if (m_GuideLineEnabled)
        drawGuideLine(painter);

    drawHistogram(painter);
}

void HistogramWidget::resizeEvent(QResizeEvent* evt)
{
    m_RenderWidth.store(evt->size().width(), std::memory_order_relaxed);

    if (getGradientAreaEnabled())
    {
        m_GradientArea.setX(0);
        m_GradientArea.setWidth(width());
        m_GradientArea.setY(height() - GRADIENT_AREA_HEIGHT);
        m_GradientArea.setHeight(GRADIENT_AREA_HEIGHT);
    }

    prepareHistogramsToRender();
    updateClippingInfo();

    QWidget::resizeEvent(evt);
}

void HistogramWidget::wheelEvent(QWheelEvent* ev)
{
    if (m_InteractiveZoomEnabled && m_ParentScrollArea)
    {
        // Zoom control
        if (ev->modifiers() & Qt::ControlModifier)
        {
            ev->accept();

            auto delta = ev->angleDelta();

            float factor;

            if (delta.y() > 0)
            {
                factor = 1.20f;
            }
            else
            {
                factor = 0.8f;
            }

            auto hBarPos = m_ParentScrollArea->horizontalScrollBar()->value();
            auto vBarPos = m_ParentScrollArea->verticalScrollBar()->value();

            auto pos = ev->position();
            int offsetx = hBarPos + (int)pos.x();
            int offsety = vBarPos + (int)pos.y();

            setFixedSize(this->size() * factor);
            setMaximumWidth(QWIDGETSIZE_MAX);
            setMaximumHeight(QWIDGETSIZE_MAX);

            m_ParentScrollArea->horizontalScrollBar()->setValue((offsetx * factor) - pos.x());
            m_ParentScrollArea->verticalScrollBar()->setValue((offsety * factor) - pos.y());
        }
        else
        {
            QWidget::wheelEvent(ev);
        }
    }

    else
    {
        QWidget::wheelEvent(ev);
    }
}

void HistogramWidget::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton)
    {
        ev->accept();

        beginCoordsTooltip(ev->position().toPoint());
        m_MouseLeftButtonPressed = true;
    }
    else if (ev->button() == Qt::MiddleButton)
    {
        ev->accept();

        m_LastMouseDragLocation = ev->globalPosition().toPoint();
        m_MouseMiddleButtonPressed = true;
        setCursor(Qt::CursorShape::OpenHandCursor);
    }
}

void HistogramWidget::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton)
    {
        ev->accept();

        endCoordsTooltip();
        m_MouseLeftButtonPressed = false;
    }
    else if (ev->button() == Qt::MiddleButton)
    {
        ev->accept();

        m_LastMouseDragLocation = ev->globalPosition().toPoint();
        m_MouseMiddleButtonPressed = false;
        setCursor(Qt::CursorShape::ArrowCursor);
    }
}

void HistogramWidget::mouseMoveEvent(QMouseEvent* ev)
{
    if (m_MouseLeftButtonPressed)
    {
        // Update coordinates overlay while dragging
        ev->accept();

        if (m_MouseLeftButtonPressed)
        {
            updateCoordsTooltip(ev->position().toPoint());
        }
    }
    else if (m_MouseMiddleButtonPressed)
    {
        ev->accept();

        QPoint delta = ev->globalPosition().toPoint() - m_LastMouseDragLocation;

        m_ParentScrollArea->horizontalScrollBar()->setValue(m_ParentScrollArea->horizontalScrollBar()->value() -
                                                            delta.x());
        m_ParentScrollArea->verticalScrollBar()->setValue(m_ParentScrollArea->verticalScrollBar()->value() - delta.y());
        m_LastMouseDragLocation = ev->globalPosition().toPoint();

        update();
    }
}

void HistogramWidget::drawBackground(QPainter& painter)
{
    painter.fillRect(this->rect(), getBackgroundColor());
}

void HistogramWidget::zoomIn()
{
    setFixedSize(size() * 1.30f);
    setMaximumWidth(QWIDGETSIZE_MAX);
    setMaximumHeight(QWIDGETSIZE_MAX);

    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

void HistogramWidget::zoomOut()
{
    setFixedSize(size() * 0.70f);
    setMaximumWidth(QWIDGETSIZE_MAX);
    setMaximumHeight(QWIDGETSIZE_MAX);

    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

void HistogramWidget::resetZoom()
{
    setFixedSize(parentWidget()->size());
    setMaximumWidth(QWIDGETSIZE_MAX);
    setMaximumHeight(QWIDGETSIZE_MAX);

    SAFE_INVOKE_METHOD_IN_GUI_THREAD(this, update);
}

void HistogramWidget::drawGrid(QPainter& painter)
{
    QPen pen;
    pen.setStyle(Qt::DotLine);
    pen.setColor(an::ui::tokens().colors.borderPrimary);
    pen.setWidthF(m_GridWidth);

    painter.setPen(pen);

    int gridSizeW = (int)(width() / m_NumOfGridsW);

    int gridSizeH = (int)(getPlotHeight() / m_NumOfGridsH);

    if (!gridSizeW || !gridSizeH)
        return;

    for (int x = 0; x < width(); x += gridSizeW)
    {
        if (pen.style() == Qt::DotLine)
            pen.setStyle(Qt::SolidLine);
        else
            pen.setStyle(Qt::DotLine);

        painter.setPen(pen);
        painter.drawLine(x, 0, x, getPlotHeight());
    }

    pen.setStyle(Qt::DotLine);
    painter.setPen(pen);

    // Draw horizontal lines
    for (int y = 0; y < getPlotHeight(); y += gridSizeH)
    {
        if (pen.style() == Qt::DotLine)
            pen.setStyle(Qt::SolidLine);
        else
            pen.setStyle(Qt::DotLine);

        painter.setPen(pen);
        painter.drawLine(0, y, width(), y);
    }
}

Qt::PenStyle HistogramWidget::channelStroke(const std::size_t channel, const bool night)
{
    if (!night)
    {
        return Qt::SolidLine;
    }
    // Where hue told the channels apart, the stroke does: red solid, green dashed, blue dotted.
    switch (channel)
    {
    case 1u:
        return Qt::DashLine;
    case 2u:
        return Qt::DotLine;
    case 0u:
        return Qt::SolidLine;
    default:
        return Qt::DashDotLine;
    }
}

void HistogramWidget::drawHistogram(QPainter& painter)
{
    if (!m_Histogram->isValid())
        return;

    QPen pen;

    pen.setStyle(Qt::SolidLine);
    pen.setWidthF(1);

    const bool isMono = m_Histogram->channels.size() == 1;
    // The curves' colours are the plot's; under Night they are all red, and each channel is told apart by its stroke.
    const auto& plot = an::ui::tokens().colors;
    const bool night = an::ui::isNightMode();

    // Enable additive blending for overlapping areas to avoid darkening
    QPainter::CompositionMode oldMode = painter.compositionMode();
    if (m_DrawAreasEnabled)
        painter.setCompositionMode(QPainter::CompositionMode_Plus);

    // Disable antialiasing for filled areas to keep crisp borders when using high scales
    const bool prevAA = painter.testRenderHint(QPainter::Antialiasing);
    if (m_DrawAreasEnabled && prevAA)
        painter.setRenderHint(QPainter::Antialiasing, false);

    // Compute global max among displayed channels (prepared data) when not locally normalizing
    const size_t displayedMaxPrepared =
        m_LocalChannelNormalization ? 0 : static_cast<size_t>(getMaxDisplayedGlobalValue());
    const size_t displayedMaxRaw =
        m_LocalChannelNormalization ? 0 : static_cast<size_t>(getMaxDisplayedHistogramValue());

    for (size_t i = 0; i < m_Histogram->channels.size(); ++i)
    {
        QColor color;
        if (isMono)
        {
            color = plot.plotGray;
            pen.setColor(color);
        }
        else
        {
            if (i == 0)
                color = plot.plotRed;
            else if (i == 1)
                color = plot.plotGreen;
            else if (i == 2)
                color = plot.plotBlue;
            else
                color = an::ui::colorAsShown(QColor(Qt::yellow));
            pen.setColor(color);
        }
        pen.setStyle(channelStroke(isMono ? 0u : i, night));

        if (!isMono && (i >= m_DisplayChannels.size() || !m_DisplayChannels[i]))
        {
            continue;
        }

        const an::ui::HistogramDisplayChannel& channel = m_Histogram->channels[i];

        painter.setPen(pen);

        // If we have a higher resolution histogram than the current window,
        // then prepare the histogram to fit the current window resolution
        if (channel.bins.size() > static_cast<size_t>(width()) && i < m_HistogramsToRender.size())
        {
            if (m_DrawAreasEnabled)
            {
                size_t maxForArea = m_LocalChannelNormalization ? m_HistogramsToRender[i].maxVal : displayedMaxPrepared;
                drawPreparedHistogramArea(painter, i, maxForArea, color);
            }
            else
                drawPreparedHistogram(
                    painter, i, m_LocalChannelNormalization ? m_HistogramsToRender[i].maxVal : displayedMaxPrepared);
        }
        else
        {
            // If the resolution of the histogram fits inside the window, then just draw everything as it is.
            const size_t maxForLines = m_LocalChannelNormalization ? channel.maxValue() : displayedMaxRaw;
            if (m_DrawAreasEnabled)
                drawHistogramChannelArea(painter, channel, maxForLines, color);
            else
                drawHistogramChannel(painter, channel, maxForLines);
        }
    }

    if (m_DrawAreasEnabled)
        painter.setCompositionMode(oldMode);

    if (m_DrawAreasEnabled && prevAA)
        painter.setRenderHint(QPainter::Antialiasing, true);
}

void HistogramWidget::drawHistogramChannel(QPainter& painter, const an::ui::HistogramDisplayChannel& channel,
                                           size_t globalMaxValue)
{
    size_t maxValue;
    if (m_LocalChannelNormalization)
        maxValue = channel.maxValue();
    else
        maxValue = globalMaxValue;

    if (maxValue == 0)
        return;

    const int binCount = static_cast<int>(channel.bins.size());
    const float bin_width = static_cast<float>(width()) / static_cast<float>(binCount);
    const int min_y = 0;
    const int max_y = getPlotHeight();

    for (int i = 1; i < binCount; i++)
    {
        const int x1 = std::round(bin_width * (i - 1));
        const int x2 = std::round(bin_width * i);

        const int y1 = std::clamp(
            getPlotHeight() - static_cast<int>(std::round(((double)channel.bins[i - 1] / maxValue) * getPlotHeight())),
            min_y, max_y);
        const int y2 = std::clamp(
            getPlotHeight() - static_cast<int>(std::round(((double)channel.bins[i - 0] / maxValue) * getPlotHeight())),
            min_y, max_y);

        painter.drawLine(x1, y1, x2, y2);
    }
}

void HistogramWidget::drawHistogramChannelArea(QPainter& painter, const an::ui::HistogramDisplayChannel& channel,
                                               size_t globalMaxValue, const QColor& color)
{
    size_t maxValue;
    if (m_LocalChannelNormalization)
        maxValue = channel.maxValue();
    else
        maxValue = globalMaxValue;

    if (maxValue == 0)
        return;

    const float bin_width = static_cast<float>(width()) / static_cast<float>(channel.bins.size());
    const int baseY = getPlotHeight();
    const double h = static_cast<double>(baseY);

    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    path.moveTo(0.0, h);

    QVector<QPointF> poly;
    poly.reserve(static_cast<qsizetype>(channel.bins.size()));

    for (size_t i = 0; i < channel.bins.size(); ++i)
    {
        const double x = static_cast<double>(bin_width) * static_cast<double>(i);
        const double y = h - (h * (static_cast<double>(channel.bins[i]) / static_cast<double>(maxValue)));
        const double yc = std::clamp(y, 0.0, h);
        const QPointF pt(x, yc);
        path.lineTo(pt);
        poly.push_back(pt);
    }

    const double lastX = std::round(static_cast<double>(bin_width) * static_cast<double>(channel.bins.size() - 1));
    path.lineTo(QPointF(lastX, h));
    path.lineTo(QPointF(0.0, h));
    path.closeSubpath();

    QColor fill = color;
    fill.setAlphaF(std::clamp(m_AreaOpacity, 0.0f, 1.0f));

    QBrush brush(fill);
    QPen oldPen = painter.pen();
    painter.setPen(Qt::NoPen);
    painter.fillPath(path, brush);

    // Stroke the curve to match the line rendering height exactly, in the channel's stroke
    QPen stroke = oldPen;
    stroke.setColor(color);
    stroke.setWidthF(1.0);
    painter.setPen(stroke);
    if (!poly.isEmpty())
        painter.drawPolyline(poly.constData(), poly.size());

    painter.setPen(oldPen);
}

void HistogramWidget::drawPreparedHistogram(QPainter& painter, size_t channelIdx, double globalMaxValue)
{
    double maxValue;
    if (m_LocalChannelNormalization)
        maxValue = m_HistogramsToRender[channelIdx].maxVal;
    else
        maxValue = globalMaxValue;

    const double plotHeight = static_cast<double>(getPlotHeight());
    auto data = m_HistogramsToRender[channelIdx].histogram_data;
    for (int x = 1; x < static_cast<int>(data->size()); ++x)
    {
        const double y1 = plotHeight - plotHeight * data->at(x - 1) / maxValue;
        const double y2 = plotHeight - plotHeight * data->at(x) / maxValue;

        painter.drawLine(x - 1, y1, x, y2);
    }
}

void HistogramWidget::drawPreparedHistogramArea(QPainter& painter, size_t channelIdx, double globalMaxValue,
                                                const QColor& color)
{
    // Ensure we have a valid max to avoid division by zero
    double maxValue;
    if (m_LocalChannelNormalization)
        maxValue = m_HistogramsToRender[channelIdx].maxVal;
    else
        maxValue = static_cast<double>(globalMaxValue);

    if (maxValue <= 0.0)
        return;

    const double h = static_cast<double>(getPlotHeight());

    auto data = m_HistogramsToRender[channelIdx].histogram_data;

    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    path.moveTo(0.0, h);

    const int n = static_cast<int>(data->size());
    QVector<QPointF> poly;
    poly.reserve(n);

    for (int x = 0; x < n; ++x)
    {
        const double val = data->at(x);
        const double y = h - (h * (val / maxValue));
        const double yc = std::clamp(y, 0.0, h);
        const QPointF pt(static_cast<double>(x), yc);
        path.lineTo(pt);
        poly.push_back(pt);
    }

    path.lineTo(QPointF((std::max)(0, n - 1), h));
    path.lineTo(QPointF(0.0, h));
    path.closeSubpath();

    QColor fill = color;
    fill.setAlphaF(std::clamp(m_AreaOpacity, 0.0f, 1.0f));

    QBrush brush(fill);
    QPen oldPen = painter.pen();
    painter.setPen(Qt::NoPen);
    painter.fillPath(path, brush);

    // Stroke the curve to match the line rendering height exactly, in the channel's stroke
    QPen stroke = oldPen;
    stroke.setColor(color);
    stroke.setWidthF(1.0);
    painter.setPen(stroke);
    if (!poly.isEmpty())
        painter.drawPolyline(poly.constData(), poly.size());

    painter.setPen(oldPen);
}

void HistogramWidget::drawGuideLine(QPainter& painter)
{
    QPen pen;

    pen.setStyle(Qt::DotLine);
    pen.setColor(an::ui::tokens().colors.textPrimary);
    pen.setWidthF(m_GridWidth);

    painter.setPen(pen);

    painter.drawLine(0, getPlotHeight(), width(), 0);
}

void HistogramWidget::drawGradientArea(QPainter& painter)
{
    const auto& colors = an::ui::tokens().colors;
    QLinearGradient gradient(0, 0, m_GradientArea.width(), 0);
    gradient.setColorAt(0.0, Qt::black);

    if (m_DisplayChannels[0] && !m_DisplayChannels[1] && !m_DisplayChannels[2])
    {
        gradient.setColorAt(1.0, colors.plotRed);
    }
    else if (!m_DisplayChannels[0] && m_DisplayChannels[1] && !m_DisplayChannels[2])
    {
        gradient.setColorAt(1.0, colors.plotGreen);
    }
    else if (!m_DisplayChannels[0] && !m_DisplayChannels[1] && m_DisplayChannels[2])
    {
        gradient.setColorAt(1.0, colors.plotBlue);
    }
    else
    {
        gradient.setColorAt(1.0, colors.plotGray);
    }

    painter.fillRect(m_GradientArea, gradient);
}

void HistogramWidget::prepareHistogramsToRender()
{
    const int render_width = currentRenderWidth();
    if (render_width <= 0)
    {
        return;
    }

    if (m_HistogramsToRender.size() != m_Histogram->channels.size())
    {
        m_HistogramsToRender.clear();
        m_HistogramsToRender.reserve(m_Histogram->channels.size());

        for (size_t i = 0; i < m_Histogram->channels.size(); ++i)
        {
            m_HistogramsToRender.push_back(histogram_render_job());
        }
    }

    for (size_t i = 0; i < m_Histogram->channels.size(); ++i)
    {
        const an::ui::HistogramDisplayChannel& channel = m_Histogram->channels[i];

        // Don't prepare anything... we won't use it if the histogram resolution
        // is lower than the current window resolution. Just abandon this loop right now
        if (channel.bins.size() <= static_cast<size_t>(render_width))
            return;

        if (m_HistogramsToRender[i].histogram_data->size() != static_cast<size_t>(render_width))
            m_HistogramsToRender[i].histogram_data->resize(static_cast<size_t>(render_width), 0.0);

        size_t j = 0;
        double graph_height = 0.f;
        size_t current_bin = 0;
        size_t nb_orig_bins = channel.bins.size();

        double vals_per_px = static_cast<double>(nb_orig_bins - 1) / static_cast<double>(render_width);

        do
        {
            double bin_val = 0.f;
            while (j < nb_orig_bins && (double)j / vals_per_px <= (double)current_bin + 0.5f)
            {
                bin_val += static_cast<double>(channel.bins[j]);
                j++;
            }
            (*m_HistogramsToRender[i].histogram_data)[current_bin] = bin_val;
            if (bin_val > graph_height && current_bin > 0 && current_bin < nb_orig_bins - 1)
                graph_height = bin_val;
            current_bin++;
        } while (j < nb_orig_bins && current_bin < m_HistogramsToRender[i].histogram_data->size());

        m_HistogramsToRender[i].maxVal = graph_height;
    }
}

int HistogramWidget::currentRenderWidth() const
{
    return m_RenderWidth.load(std::memory_order_relaxed);
}

void HistogramWidget::updateClippingInfo()
{
    if (!m_Histogram->isValid())
        return;

    std::uint64_t currentClippedLow = 0;
    std::uint64_t currentClippedHigh = 0;
    std::uint64_t samples = 0;
    for (const an::ui::HistogramDisplayChannel& channel : m_Histogram->channels)
    {
        currentClippedLow += channel.clippedLow;
        currentClippedHigh += channel.clippedHigh;
        samples += channel.samples;
    }

    // A share of the samples the image's validity admitted; an image that admits none clips none.
    const double total = samples > 0 ? static_cast<double>(samples) : 1.0;
    double lowPercent = static_cast<double>(currentClippedLow) * 100.0 / total;
    double highPercent = static_cast<double>(currentClippedHigh) * 100.0 / total;

    QString clipInfo = QString("Clipping Low: %1%\nClipping High: %2%")
                           .arg(QString::number(lowPercent, 'f', 3))
                           .arg(QString::number(highPercent, 'f', 3));
    SAFE_INVOKE_METHOD_IN_GUI_THREAD(m_ClippingOverlay, setText, clipInfo);
}

double HistogramWidget::getMaxGlobalValue() const
{
    double maxVal = 0;

    for (auto& channel : m_HistogramsToRender)
    {
        maxVal = (std::max)(channel.maxVal, maxVal);
    }

    return maxVal;
}

double HistogramWidget::getMaxDisplayedGlobalValue() const
{
    double maxVal = 0;
    for (size_t i = 0; i < m_HistogramsToRender.size(); ++i)
    {
        if (i < 4 && !m_DisplayChannels[i])
            continue;
        maxVal = (std::max)(maxVal, m_HistogramsToRender[i].maxVal);
    }
    return maxVal;
}

int HistogramWidget::getPlotHeight()
{
    if (getGradientAreaEnabled())
        return height() - m_GradientArea.height();

    return height();
}

void HistogramWidget::beginCoordsTooltip(const QPoint& localPos)
{
    setCursor(Qt::CursorShape::CrossCursor);

    // Ensure overlay exists
    if (!m_CoordsOverlay)
    {
        m_CoordsOverlay = new HistogramOverlay(this);
    }
    refreshOverlayColors();

    m_CoordsOverlay->show();

    // Show coordinates overlay near the cursor
    showTooltipAt(localPos);
}

void HistogramWidget::updateCoordsTooltip(const QPoint& localPos)
{
    showTooltipAt(localPos);
}

void HistogramWidget::endCoordsTooltip()
{
    setCursor(Qt::CursorShape::ArrowCursor);

    if (m_CoordsOverlay)
        m_CoordsOverlay->hide();
}

void HistogramWidget::refreshOverlayColors()
{
    const auto& colors = an::ui::tokens().colors;
    if (m_ClippingOverlay)
    {
        m_ClippingOverlay->setBackgroundColor(withAlpha(colors.bgPrimary, 80));
        m_ClippingOverlay->setFontColor(colors.statusWarning);
    }

    if (m_CoordsOverlay)
    {
        m_CoordsOverlay->setBackgroundColor(withAlpha(colors.bgSurface, 200));
        m_CoordsOverlay->setFontColor(colors.textPrimary);
    }
}

// Centralized tooltip/overlay helper
void HistogramWidget::showTooltipAt(const QPoint& localPos)
{
    if (!m_Histogram || !m_Histogram->isValid())
        return;

    const int w = width();
    if (w <= 1)
        return;

    const int x = std::clamp(localPos.x(), 0, w - 1);

    const int nb = static_cast<int>(m_Histogram->channels.front().bins.size());
    if (nb <= 0)
        return;

    const int bin =
        std::clamp((int)std::llround((double)x * (double)(nb - 1) / (double)(std::max)(1, w - 1)), 0, nb - 1);

    const bool isMono = m_Histogram->channels.size() == 1;

    // Normalized X in [0,1]
    const double xNorm = (nb > 1) ? (static_cast<double>(bin) / static_cast<double>(nb - 1)) : 0.0;

    QString parts; // per-channel normalized values string for RGB(A)

    // Precompute displayed global max for prepared data when global normalization is active
    const double displayedMaxPrepared = m_LocalChannelNormalization ? 0.0 : getMaxDisplayedGlobalValue();

    double monoValNorm = 0.0; // used in mono case

    for (int i = 0; i < static_cast<int>(m_Histogram->channels.size()); ++i)
    {
        if (!isMono && (i >= 4 || !m_DisplayChannels[i]))
            continue;

        const an::ui::HistogramDisplayChannel& ch = m_Histogram->channels[static_cast<size_t>(i)];

        // Obtain value at position
        double val = 0.0;
        bool prepared = ch.bins.size() > static_cast<size_t>(w) && static_cast<size_t>(i) < m_HistogramsToRender.size();
        if (prepared)
        {
            auto& prep = m_HistogramsToRender[i];
            const int n = static_cast<int>(prep.histogram_data->size());
            if (n > 0)
            {
                const int xi = std::clamp(x, 0, n - 1);
                double dv = prep.histogram_data->at(xi);
                if (dv < 0.0)
                    dv = 0.0;
                val = dv;
            }
        }
        else if (bin < static_cast<int>(ch.bins.size()))
        {
            val = static_cast<double>(ch.bins[static_cast<size_t>(bin)]);
        }

        // Determine denominator consistent with rendering
        double denom = 0.0;
        if (prepared)
        {
            denom = m_LocalChannelNormalization ? m_HistogramsToRender[i].maxVal : displayedMaxPrepared;
        }
        else
        {
            denom = m_LocalChannelNormalization ? static_cast<double>(ch.maxValue()) : getMaxDisplayedHistogramValue();
        }

        double norm = (denom > 0.0) ? std::clamp(val / denom, 0.0, 1.0) : 0.0;

        if (isMono)
        {
            monoValNorm = norm; // keep last (only) channel
        }
        else
        {
            if (!parts.isEmpty())
                parts += "  ";
            const QChar label = (i == 0 ? QChar('R') : i == 1 ? QChar('G') : i == 2 ? QChar('B') : QChar('A'));
            parts += QString("%1 %2").arg(label).arg(QString::number(norm, 'f', 3));
        }
    }

    QString text;
    if (isMono)
        text = QString("x: %1, y: %2").arg(QString::number(xNorm, 'f', 3)).arg(QString::number(monoValNorm, 'f', 3));
    else
        text = QString("x: %1, y: %2").arg(QString::number(xNorm, 'f', 3)).arg(parts);

    if (!m_CoordsOverlay)
        return;

    m_CoordsOverlay->setText(text);
    // Keep inside widget bounds if needed
    QPoint anchor = localPos + QPoint(5, 18);
    // Clamp anchor to prevent overlay from going outside the widget area
    const QSize hint = m_CoordsOverlay->sizeHint();
    anchor.setX(std::clamp(anchor.x(), 0, (std::max)(0, width() - hint.width() - 16)));
    anchor.setY(std::clamp(anchor.y(), 0, (std::max)(0, height() - hint.height() - 16)));
    m_CoordsOverlay->setAnchor(anchor);
    if (!m_CoordsOverlay->isVisible())
        m_CoordsOverlay->show();
}
