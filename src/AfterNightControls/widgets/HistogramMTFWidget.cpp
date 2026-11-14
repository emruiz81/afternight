// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "HistogramMTFWidget.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightEngine/Algorithms/Pointwise/MidtonesTransfer.h>

constexpr int RANGE_SELECTOR_HEIGHT = 14;

HistogramMTFWidget::HistogramMTFWidget(QScrollArea* parent) : HistogramWidget(nullptr)
{
    setMouseTracking(true);
    setParentScrollArea(parent);
    setInteractiveZoomEnabled(true);

    m_RangeSelectorArea.setX(0);
    m_RangeSelectorArea.setWidth(width());
    m_RangeSelectorArea.setY(height() - RANGE_SELECTOR_HEIGHT);
    m_RangeSelectorArea.setHeight(RANGE_SELECTOR_HEIGHT);
}

void HistogramMTFWidget::setTransferCurveEnabled(bool enable)
{
    m_TransferCurveEnabled = enable;
    update();
}

bool HistogramMTFWidget::getTransferCurveEnabled() const
{
    return m_TransferCurveEnabled;
}

void HistogramMTFWidget::setRangeSelectorEnabled(bool enabled)
{
    m_RangeSelectorEnabled = enabled;
}

bool HistogramMTFWidget::getRangeSelectorEnabled() const
{
    return m_RangeSelectorEnabled;
}

void HistogramMTFWidget::setValues(double shadows, double midtones, double highlights)
{
    m_ShadowsValue = shadows;
    m_MidtonesValue = midtones;
    m_HighlightsValue = highlights;
    update();
}

void HistogramMTFWidget::setShadowsValue(double value)
{
    m_ShadowsValue = value;
    update();
}

void HistogramMTFWidget::setMidtonesValue(double value)
{
    m_MidtonesValue = value;
    update();
}

void HistogramMTFWidget::setHighlightsValue(double value)
{
    m_HighlightsValue = value;
    update();
}

void HistogramMTFWidget::setListener(HistogramMTFWidgetListener* listener)
{
    m_Listener = listener;
}

HistogramMTFWidgetListener* HistogramMTFWidget::getListener() const
{
    return m_Listener;
}

HistogramMTFWidget::~HistogramMTFWidget()
{
    setMouseTracking(false);
}

void HistogramMTFWidget::paintEvent(QPaintEvent* evt)
{
    evt->accept();

    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, true);

    drawBackground(painter);

    if (m_RangeSelectorEnabled)
        drawRangeSelector(painter);

    if (m_GridEnabled)
        drawGrid(painter);

    drawHistogram(painter);

    if (m_GuideLineEnabled)
        drawGuideLine(painter);

    if (m_TransferCurveEnabled)
        drawTransferCurve(painter);
}

void HistogramMTFWidget::resizeEvent(QResizeEvent* evt)
{
    m_RangeSelectorArea.setX(0);
    m_RangeSelectorArea.setY(evt->size().height() - RANGE_SELECTOR_HEIGHT);
    m_RangeSelectorArea.setWidth(evt->size().width());
    m_RangeSelectorArea.setHeight(RANGE_SELECTOR_HEIGHT);

    // The base records the width a dense histogram is prepared for before it prepares one.
    HistogramWidget::resizeEvent(evt);
}

void HistogramMTFWidget::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton)
    {
        m_MouseLeftButtonPressed = true;

        ev->accept();
        m_CurrentPointerDragStatus = getPointerDragStatus(ev->pos());
        if (m_CurrentPointerDragStatus == pointer_drag_status::no_drag)
        {
            beginCoordsTooltip(ev->position().toPoint());
        }
        else
        {
            setCursor(Qt::ClosedHandCursor);
        }
    }
    else if (ev->button() == Qt::RightButton)
    {
        ev->accept();
        resetZoom();
    }
    else if (ev->button() == Qt::MiddleButton)
    {
        ev->accept();

        m_LastMouseDragLocation = ev->globalPosition().toPoint();
        m_MouseMiddleButtonPressed = true;
        setCursor(Qt::CursorShape::OpenHandCursor);
    }
}

void HistogramMTFWidget::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton)
    {
        m_MouseLeftButtonPressed = false;
        ev->accept();
        if (m_CurrentPointerDragStatus != pointer_drag_status::no_drag)
        {
            if (m_Listener)
            {
                m_Listener->onSettingsUpdated();
            }

            m_CurrentPointerDragStatus = pointer_drag_status::no_drag;

            auto tmpDragStatus = getPointerDragStatus(ev->pos());
            if (tmpDragStatus != pointer_drag_status::no_drag)
            {
                setCursor(Qt::CursorShape::PointingHandCursor);
            }
            else
            {
                setCursor(Qt::CursorShape::ArrowCursor);
            }
        }
        else
        {
            endCoordsTooltip();
        }
    }
    else if (ev->button() == Qt::MiddleButton)
    {
        ev->accept();

        m_LastMouseDragLocation = ev->globalPosition().toPoint();
        m_MouseMiddleButtonPressed = false;
        setCursor(Qt::CursorShape::ArrowCursor);
    }
}

void HistogramMTFWidget::mouseMoveEvent(QMouseEvent* ev)
{
    if (m_MouseMiddleButtonPressed)
    {
        ev->accept();

        QPoint delta = ev->globalPosition().toPoint() - m_LastMouseDragLocation;

        m_ParentScrollArea->horizontalScrollBar()->setValue(m_ParentScrollArea->horizontalScrollBar()->value() -
                                                            delta.x());
        m_ParentScrollArea->verticalScrollBar()->setValue(m_ParentScrollArea->verticalScrollBar()->value() - delta.y());
        m_LastMouseDragLocation = ev->globalPosition().toPoint();

        update();
    }
    else if (m_MouseLeftButtonPressed)
    {
        ev->accept();

        if (m_CurrentPointerDragStatus != pointer_drag_status::no_drag)
        {
            if (m_CurrentPointerDragStatus == pointer_drag_status::shadows_drag)
            {
                int pixels = ev->pos().x();
                double value = (double)pixels / (double)width();

                if (m_ShadowsValue == value)
                    return;

                m_ShadowsValue = value;

                if (m_ShadowsValue < 0.0)
                    m_ShadowsValue = 0.0;

                if (m_ShadowsValue > m_HighlightsValue)
                    m_ShadowsValue = m_HighlightsValue;

                if (m_Listener)
                    m_Listener->onShadowsChanged(m_ShadowsValue);
            }
            else if (m_CurrentPointerDragStatus == pointer_drag_status::midtones_drag)
            {
                int pixels = ev->pos().x();
                double midtonesAbsolute = (double)pixels / (double)width();
                if (midtonesAbsolute < m_ShadowsValue)
                    midtonesAbsolute = m_ShadowsValue;

                if (midtonesAbsolute > m_HighlightsValue)
                    midtonesAbsolute = m_HighlightsValue;

                double value = getRelativeMidtones(midtonesAbsolute);

                if (m_MidtonesValue == value)
                    return;

                m_MidtonesValue = value;

                if (m_Listener)
                    m_Listener->onMidtonesChanged(m_MidtonesValue);
            }
            else if (m_CurrentPointerDragStatus == pointer_drag_status::highlights_drag)
            {
                int pixels = ev->pos().x();
                double value = (double)pixels / (double)width();

                if (m_HighlightsValue == value)
                    return;

                m_HighlightsValue = value;

                if (m_HighlightsValue > 1.0)
                    m_HighlightsValue = 1.0;

                if (m_HighlightsValue < m_ShadowsValue)
                    m_HighlightsValue = m_ShadowsValue;

                if (m_Listener)
                    m_Listener->onHightlightsChanged(m_HighlightsValue);
            }

            setCursor(Qt::CursorShape::ClosedHandCursor);

            update();
        }
        else
        {
            updateCoordsTooltip(ev->position().toPoint());
        }
    }
    else
    {
        // Don't use m_CurrentPointerDragStatus because is only set when the mouse is pressed
        auto tmpDragStatus = getPointerDragStatus(ev->pos());
        if (tmpDragStatus != pointer_drag_status::no_drag)
        {
            setCursor(Qt::CursorShape::PointingHandCursor);
        }
        else
        {
            setCursor(Qt::CursorShape::ArrowCursor);
        }
    }
}

void HistogramMTFWidget::drawTransferCurve(QPainter& painter)
{
    QPen pen;

    pen.setStyle(Qt::SolidLine);
    pen.setWidthF(1.0);
    pen.setColor(an::ui::tokens().colors.plotGray);

    painter.setPen(pen);

    int plotHeight = getPlotHeight();
    int origin = m_ShadowsValue * width();
    QPoint previousPos(origin, plotHeight);

    // The curve the transformation applies, evaluated by the engine; a point the engine refuses - a midtone that a
    // collapsed range leaves undefined - is not drawn.
    const an::MidtonesTransferParameters parameters{
        .midtones = m_MidtonesValue, .shadows = m_ShadowsValue, .highlights = m_HighlightsValue};
    for (int i = 0; i < width(); ++i)
    {
        float x = i / (float)width();
        const an::Result<double> transferred = an::evaluateMidtonesTransfer(x, parameters);
        if (!transferred.hasValue())
            continue;
        float y = static_cast<float>(*transferred);
        QPoint newPos(x * width(), plotHeight * (1 - y));

        if (newPos.x() < origin)
            continue;

        painter.drawLine(previousPos, newPos);
        previousPos = newPos;
    }
}

void HistogramMTFWidget::drawRangeSelector(QPainter& painter)
{
    const auto& colors = an::ui::tokens().colors;
    // Draw the box
    QLinearGradient gradient(0, 0, m_RangeSelectorArea.width(), 0);
    gradient.setColorAt(0.0, colors.bgPrimary);
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
        gradient.setColorAt(1.0, colors.textPrimary);
    }

    painter.fillRect(m_RangeSelectorArea, gradient);

    // Draw the pointers

    QPoint middlePoint(0, height() - (RANGE_SELECTOR_HEIGHT / 2)); // Middle point of the widget

    middlePoint.setX(width() * m_ShadowsValue);
    drawRangeSelectorPointer(painter, middlePoint);

    middlePoint.setX(width() * getAbsoluteMidtones());
    drawRangeSelectorPointer(painter, middlePoint);

    middlePoint.setX(width() * m_HighlightsValue);
    drawRangeSelectorPointer(painter, middlePoint);
}

void HistogramMTFWidget::drawRangeSelectorPointer(QPainter& painter, QPoint& pos)
{
    const auto& colors = an::ui::tokens().colors;
    QPen pen;

    pen.setStyle(Qt::SolidLine);
    pen.setWidthF(1.5);
    if (m_DisplayChannels[0] && !m_DisplayChannels[1] && !m_DisplayChannels[2])
    {
        pen.setColor(colors.textPrimary);
    }
    else if (!m_DisplayChannels[0] && m_DisplayChannels[1] && !m_DisplayChannels[2])
    {
        pen.setColor(colors.textPrimary);
    }
    else if (!m_DisplayChannels[0] && !m_DisplayChannels[1] && m_DisplayChannels[2])
    {
        pen.setColor(colors.textPrimary);
    }
    else
    {
        if (pos.x() < ((float)width() / 2.0f))
            pen.setColor(colors.textPrimary);
        else
            pen.setColor(colors.bgPrimary);
    }

    painter.setPen(pen);

    const int triangleSize = RANGE_SELECTOR_HEIGHT; // Adjust the size as needed

    // Calculate the coordinates of the triangle vertices
    QPoint vertex1(pos.x(), pos.y() - triangleSize / 2);
    QPoint vertex2(pos.x() + triangleSize / 2, pos.y() + triangleSize / 2);
    QPoint vertex3(pos.x() - triangleSize / 2, pos.y() + triangleSize / 2);

    // Draw the triangle using the calculated vertices
    painter.drawPolygon(QPolygon({vertex1, vertex2, vertex3}));
}

double HistogramMTFWidget::getRelativeMidtones(double absMidtones)
{
    double maxRange = m_HighlightsValue - m_ShadowsValue;

    double relativeMidtones = (absMidtones - m_ShadowsValue) / maxRange;

    return relativeMidtones;
}

double HistogramMTFWidget::getAbsoluteMidtones()
{
    double maxRange = m_HighlightsValue - m_ShadowsValue;

    double absMidtones = m_ShadowsValue + (maxRange * m_MidtonesValue);

    return absMidtones;
}

HistogramMTFWidget::pointer_drag_status HistogramMTFWidget::getPointerDragStatus(const QPoint& pos)
{
    int halfSize = (RANGE_SELECTOR_HEIGHT / 2);

    QRect shadowsPointer((m_ShadowsValue * width()) - halfSize, height() - RANGE_SELECTOR_HEIGHT, RANGE_SELECTOR_HEIGHT,
                         RANGE_SELECTOR_HEIGHT);

    if (shadowsPointer.contains(pos))
    {
        return pointer_drag_status::shadows_drag;
    }
    else
    {
        QRect midtonesPointer((getAbsoluteMidtones() * width()) - halfSize, height() - RANGE_SELECTOR_HEIGHT,
                              RANGE_SELECTOR_HEIGHT, RANGE_SELECTOR_HEIGHT);

        if (midtonesPointer.contains(pos))
        {
            return pointer_drag_status::midtones_drag;
        }
        else
        {
            QRect highlightsPointer((m_HighlightsValue * width()) - halfSize, height() - RANGE_SELECTOR_HEIGHT,
                                    RANGE_SELECTOR_HEIGHT, RANGE_SELECTOR_HEIGHT);

            if (highlightsPointer.contains(pos))
            {
                return pointer_drag_status::highlights_drag;
            }
        }
    }

    return pointer_drag_status::no_drag;
}

int HistogramMTFWidget::getPlotHeight()
{
    if (m_RangeSelectorEnabled)
        return height() - m_RangeSelectorArea.height();

    return height();
}
