// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "HistogramGHTWidget.h"
#include "common.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>

constexpr int RANGE_SELECTOR_HEIGHT = 14;

HistogramGHTWidget::HistogramGHTWidget(QScrollArea* parent) : HistogramWidget(nullptr)
{
    setMouseTracking(true);
    setParentScrollArea(parent);
    setInteractiveZoomEnabled(true);

    m_RangeSelectorArea.setX(0);
    m_RangeSelectorArea.setWidth(width());
    m_RangeSelectorArea.setY(height() - RANGE_SELECTOR_HEIGHT);
    m_RangeSelectorArea.setHeight(RANGE_SELECTOR_HEIGHT);
}

void HistogramGHTWidget::setTransferCurveEnabled(bool enable)
{
    m_TransferCurveEnabled = enable;
    update();
}

bool HistogramGHTWidget::getTransferCurveEnabled() const
{
    return m_TransferCurveEnabled;
}

void HistogramGHTWidget::setRangeSelectorEnabled(bool enabled)
{
    m_RangeSelectorEnabled = enabled;
    update();
}

bool HistogramGHTWidget::getRangeSelectorEnabled() const
{
    return m_RangeSelectorEnabled;
}

void HistogramGHTWidget::setValues(double shadows, double midtones, double highlights)
{
    m_ProtectShadowsValue = shadows;
    m_SymmetryPointValue = midtones;
    m_ProtectHighlightsValue = highlights;
    update();
}

void HistogramGHTWidget::setProtectShadowsValue(double value)
{
    m_ProtectShadowsValue = value;
    update();
}

void HistogramGHTWidget::setSymmetryPointValue(double value)
{
    m_SymmetryPointValue = value;
    if (m_SymmetryPointValue < m_ProtectShadowsValue)
        m_ProtectShadowsValue = m_SymmetryPointValue;

    if (m_SymmetryPointValue > m_ProtectHighlightsValue)
        m_ProtectHighlightsValue = m_SymmetryPointValue;

    update();
}

void HistogramGHTWidget::setProtectHighlightsValue(double value)
{
    m_ProtectHighlightsValue = value;
    update();
}

void HistogramGHTWidget::setSPPickerEnabled(bool enable)
{
    m_SPPickerEnabled = enable;
    if (m_SPPickerEnabled)
    {
        setCursor(createSolidCrossCursor());
    }
    else
    {
        setCursor(Qt::ArrowCursor);
    }

    m_SPPickerSelection.setX(0);
    m_SPPickerSelection.setY(0);

    update();
}

bool HistogramGHTWidget::getSPPickerEnabled() const
{
    return m_SPPickerEnabled;
}

void HistogramGHTWidget::setListener(HistogramGHTWidgetListener* listener)
{
    m_Listener = listener;
    update();
}

HistogramGHTWidgetListener* HistogramGHTWidget::getListener() const
{
    return m_Listener;
}

HistogramGHTWidget::~HistogramGHTWidget()
{
    setMouseTracking(false);
}

void HistogramGHTWidget::paintEvent(QPaintEvent* evt)
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

    if (m_SPPickerEnabled)
        drawSPPickerSelection(painter);

    if (m_GuideLineEnabled)
        drawGuideLine(painter);

    if (m_TransferCurveEnabled)
        drawTransferCurve(painter);
}

void HistogramGHTWidget::resizeEvent(QResizeEvent* evt)
{
    m_RangeSelectorArea.setX(0);
    m_RangeSelectorArea.setY(evt->size().height() - RANGE_SELECTOR_HEIGHT);
    m_RangeSelectorArea.setWidth(evt->size().width());
    m_RangeSelectorArea.setHeight(RANGE_SELECTOR_HEIGHT);

    // The base records the width a dense histogram is prepared for before it prepares one.
    HistogramWidget::resizeEvent(evt);
}

void HistogramGHTWidget::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton)
    {
        m_MouseLeftButtonPressed = true;

        ev->accept();
        m_CurrentPointerDragStatus = getPointerDragStatus(ev->pos());

        if (m_SPPickerEnabled)
        {
            m_SPPickerSelection = ev->pos();

            if (m_Listener)
            {
                int pixels = ev->pos().x();
                double value = (double)pixels / (double)width();
                m_Listener->onSPPickerUpdated(value);
            }
        }
        else
        {
            if (m_CurrentPointerDragStatus == pointer_drag_status::no_drag)
            {
                beginCoordsTooltip(ev->position().toPoint());
            }
            else
            {
                setCursor(Qt::ClosedHandCursor);
            }
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

void HistogramGHTWidget::mouseReleaseEvent(QMouseEvent* ev)
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

void HistogramGHTWidget::mouseMoveEvent(QMouseEvent* ev)
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
        if (m_CurrentPointerDragStatus != pointer_drag_status::no_drag)
        {
            if (m_CurrentPointerDragStatus == pointer_drag_status::shadows_drag)
            {
                int pixels = ev->pos().x();
                double value = (double)pixels / (double)width();

                if (m_ProtectShadowsValue == value)
                    return;

                m_ProtectShadowsValue = value;

                if (m_ProtectShadowsValue < 0.0)
                    m_ProtectShadowsValue = 0.0;

                if (m_ProtectShadowsValue > m_SymmetryPointValue)
                    m_ProtectShadowsValue = m_SymmetryPointValue;

                if (m_Listener)
                    m_Listener->onProtectShadowsChanged(m_ProtectShadowsValue);
            }
            else if (m_CurrentPointerDragStatus == pointer_drag_status::symmetry_point_drag)
            {
                int pixels = ev->pos().x();
                double value = (double)pixels / (double)width();

                if (m_SymmetryPointValue == value)
                    return;

                m_SymmetryPointValue = value;

                if (m_SymmetryPointValue < 0.0)
                    m_SymmetryPointValue = 0.0;
                else if (m_SymmetryPointValue > 1.0)
                    m_SymmetryPointValue = 1.0;

                if (m_SymmetryPointValue < m_ProtectShadowsValue)
                {
                    m_ProtectShadowsValue = m_SymmetryPointValue;
                }

                if (m_SymmetryPointValue > m_ProtectHighlightsValue)
                {
                    m_ProtectHighlightsValue = m_SymmetryPointValue;
                }

                if (m_Listener)
                    m_Listener->onSymmetryPointChanged(m_SymmetryPointValue);
            }
            else if (m_CurrentPointerDragStatus == pointer_drag_status::highlights_drag)
            {
                int pixels = ev->pos().x();
                double value = (double)pixels / (double)width();

                if (m_ProtectHighlightsValue == value)
                    return;

                m_ProtectHighlightsValue = value;

                if (m_ProtectHighlightsValue > 1.0)
                    m_ProtectHighlightsValue = 1.0;

                if (m_ProtectHighlightsValue < m_SymmetryPointValue)
                    m_ProtectHighlightsValue = m_SymmetryPointValue;

                if (m_Listener)
                    m_Listener->onProtectHightlightsChanged(m_ProtectHighlightsValue);
            }

            setCursor(Qt::CursorShape::ClosedHandCursor);

            update();
        }
        else if (!m_SPPickerEnabled)
        {
            ev->accept();
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

void HistogramGHTWidget::drawTransferCurve(QPainter& painter)
{
    if (!m_Listener)
        return;

    QPen pen;

    // The plot is always drawn on the dark background, so the curve takes the dark theme's text colour in either theme.
    pen.setStyle(Qt::SolidLine);
    pen.setWidthF(1.0);
    pen.setColor(an::ui::darkTokens().colors.textPrimary);

    painter.setPen(pen);

    int plotHeight = getPlotHeight();
    int origin = 0;
    QPoint previousPos(origin, plotHeight);

    // The curve the transformation applies, evaluated by the engine for the channel the listener names - which picks
    // the screen transfer's triplet; a point the engine refuses is not drawn.
    const an::GeneralizedStretchParameters params = m_Listener->stretchParameters();
    const std::uint32_t transferCurveChannel = m_Listener->transferCurveChannel();

    for (int i = 0; i < width(); ++i)
    {
        float x = i / (float)width();
        const an::Result<double> stretched = an::evaluateGeneralizedStretch(x, params, transferCurveChannel);
        if (!stretched.hasValue())
            continue;
        float y = static_cast<float>(*stretched);
        QPoint newPos(x * width(), plotHeight * (1 - y));

        if (newPos.x() < origin)
            continue;

        painter.drawLine(previousPos, newPos);
        previousPos = newPos;
    }
}

void HistogramGHTWidget::drawRangeSelector(QPainter& painter)
{
    const auto& colors = an::ui::tokens().colors;
    // Draw the box
    QLinearGradient gradient(0, 0, m_RangeSelectorArea.width(), 0);
    gradient.setColorAt(0.0, colors.bgPrimary);
    if (m_DisplayChannels[0] && !m_DisplayChannels[1] && !m_DisplayChannels[2])
    {
        gradient.setColorAt(1.0, Qt::red);
    }
    else if (!m_DisplayChannels[0] && m_DisplayChannels[1] && !m_DisplayChannels[2])
    {
        gradient.setColorAt(1.0, Qt::green);
    }
    else if (!m_DisplayChannels[0] && !m_DisplayChannels[1] && m_DisplayChannels[2])
    {
        gradient.setColorAt(1.0, Qt::blue);
    }
    else
    {
        gradient.setColorAt(1.0, colors.textPrimary);
    }

    painter.fillRect(m_RangeSelectorArea, gradient);

    // Draw the pointers

    QPoint middlePoint(0, height() - (RANGE_SELECTOR_HEIGHT / 2)); // Middle point of the widget

    middlePoint.setX(width() * m_ProtectShadowsValue);
    drawRangeSelectorPointer(painter, middlePoint);

    middlePoint.setX(width() * m_SymmetryPointValue);
    drawRangeSelectorPointer(painter, middlePoint);

    middlePoint.setX(width() * m_ProtectHighlightsValue);
    drawRangeSelectorPointer(painter, middlePoint);
}

void HistogramGHTWidget::drawRangeSelectorPointer(QPainter& painter, QPoint& pos)
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

void HistogramGHTWidget::drawSPPickerSelection(QPainter& painter)
{
    if (m_SPPickerSelection.x() > 0 || m_SPPickerSelection.y() > 0)
    {
        const auto& colors = an::ui::tokens().colors;
        QPen pen;

        pen.setStyle(Qt::SolidLine);
        pen.setWidthF(2);

        pen.setColor(colors.statusWarning);
        painter.setPen(pen);

        painter.drawLine(m_SPPickerSelection.x(), 0, m_SPPickerSelection.x(), getPlotHeight());
    }
}

HistogramGHTWidget::pointer_drag_status HistogramGHTWidget::getPointerDragStatus(const QPoint& pos)
{
    int halfSize = (RANGE_SELECTOR_HEIGHT / 2);

    QRect midtonesPointer((m_SymmetryPointValue * width()) - halfSize, height() - RANGE_SELECTOR_HEIGHT,
                          RANGE_SELECTOR_HEIGHT, RANGE_SELECTOR_HEIGHT);

    if (midtonesPointer.contains(pos))
    {
        return pointer_drag_status::symmetry_point_drag;
    }
    else
    {
        QRect shadowsPointer((m_ProtectShadowsValue * width()) - halfSize, height() - RANGE_SELECTOR_HEIGHT,
                             RANGE_SELECTOR_HEIGHT, RANGE_SELECTOR_HEIGHT);

        if (shadowsPointer.contains(pos))
        {
            return pointer_drag_status::shadows_drag;
        }
        else
        {
            QRect highlightsPointer((m_ProtectHighlightsValue * width()) - halfSize, height() - RANGE_SELECTOR_HEIGHT,
                                    RANGE_SELECTOR_HEIGHT, RANGE_SELECTOR_HEIGHT);

            if (highlightsPointer.contains(pos))
            {
                return pointer_drag_status::highlights_drag;
            }
        }
    }

    return pointer_drag_status::no_drag;
}

int HistogramGHTWidget::getPlotHeight()
{
    if (m_RangeSelectorEnabled)
        return height() - m_RangeSelectorArea.height();

    return height();
}
