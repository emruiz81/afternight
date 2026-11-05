// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "CurvesWidget.h"
#include "theme/AnDesignTokens.h"

constexpr int NO_NODE_SELECTED = -1;

CurvesWidget::CurvesWidget(QScrollArea* parent) : HistogramWidget(nullptr)
{
    this->setAttribute(Qt::WA_DeleteOnClose);

    setMouseTracking(true);
    setParentScrollArea(parent);
    setInteractiveZoomEnabled(true);
}

void CurvesWidget::setGuideLineEnabled(bool enabled)
{
    m_GuideLineEnabled = enabled;
    update();
}

bool CurvesWidget::getGuideLineEnabled() const
{
    return m_GuideLineEnabled;
}

void CurvesWidget::setHistogramEnabled(bool enabled)
{
    m_HistogramEnabled = enabled;
    update();
}

bool CurvesWidget::getHistogramEnabled() const
{
    return m_HistogramEnabled;
}

CurvesWidget::~CurvesWidget()
{
    setMouseTracking(false);
}

QColor CurvesWidget::getSplineColor() const
{
    return an::ui::tokens().colors.textPrimary;
}

void CurvesWidget::paintEvent(QPaintEvent* evt)
{
    evt->accept();

    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, true);

    drawBackground(painter);

    if (m_GridEnabled)
        drawGrid(painter);

    if (m_GuideLineEnabled)
        drawGuideLine(painter);

    drawSplines(painter);

    if (m_GradientAreaEnabled)
        drawGradientArea(painter);

    if (m_HistogramEnabled)
        drawHistogram(painter);
}

void CurvesWidget::resizeEvent(QResizeEvent* evt)
{
    update_spline_to_render();
    HistogramWidget::resizeEvent(evt);
}

void CurvesWidget::mousePressEvent(QMouseEvent* ev)
{
    std::lock_guard lock(m_mutex);

    if (ev->button() == Qt::LeftButton)
    {
        m_MouseLeftButtonPressed = true;

        ev->accept();
        m_CurrentNodeSelected = getCurrentNodeSelected(ev->pos(), false);

        if (m_CurrentNodeSelected == NO_NODE_SELECTED)
        {
            if (m_HistogramEnabled)
            {
                beginCoordsTooltip(ev->position().toPoint());
            }
        }
        else
        {
            setCursor(Qt::ClosedHandCursor);
        }
    }
    else if (ev->button() == Qt::RightButton)
    {
        ev->accept();
        // resetZoom();
        auto nodeSelected = getCurrentNodeSelected(ev->pos(), false);
        if (nodeSelected != NO_NODE_SELECTED && nodeSelected > 0 &&
            nodeSelected < (static_cast<int>(m_curve.size()) - 1))
        {
            m_curve.removePoint(static_cast<size_t>(nodeSelected));
            splineUpdated();
        }
    }
    else if (ev->button() == Qt::MiddleButton)
    {
        ev->accept();

        m_LastMouseDragLocation = ev->globalPosition().toPoint();
        m_MouseMiddleButtonPressed = true;
        m_ParentScrollArea->setCursor(Qt::CursorShape::OpenHandCursor);
    }
}

void CurvesWidget::mouseReleaseEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton)
    {
        m_MouseLeftButtonPressed = false;

        ev->accept();
        if (m_CurrentNodeSelected != NO_NODE_SELECTED)
        {
            m_CurrentNodeSelected = NO_NODE_SELECTED;
            auto nodeSelected = getCurrentNodeSelected(ev->pos(), true);
            if (nodeSelected != NO_NODE_SELECTED)
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
            if (m_HistogramEnabled)
            {
                endCoordsTooltip();
            }
        }
    }
    else if (ev->button() == Qt::MiddleButton)
    {
        ev->accept();

        m_LastMouseDragLocation = ev->globalPosition().toPoint();
        m_MouseMiddleButtonPressed = false;
        m_ParentScrollArea->setCursor(Qt::CursorShape::ArrowCursor);
    }
}

void CurvesWidget::mouseMoveEvent(QMouseEvent* ev)
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

        if (m_CurrentNodeSelected != NO_NODE_SELECTED)
        {
            std::lock_guard lock(m_mutex);

            // The end points stay at the ends of the interval; an inner point is kept between its neighbours by the
            // curve itself.
            double x_pos = 0.0;
            if (m_CurrentNodeSelected == static_cast<int>(m_curve.size()) - 1)
            {
                x_pos = 1.0;
            }
            else if (m_CurrentNodeSelected > 0)
            {
                x_pos = (double)ev->pos().x() / (double)width();
            }

            double y_pos = 1.0 - ((double)ev->pos().y() / (double)getPlotHeight());

            m_curve.updatePoint(static_cast<size_t>(m_CurrentNodeSelected), x_pos, y_pos);

            splineUpdated();

            setCursor(Qt::CursorShape::ClosedHandCursor);
        }
        else
        {
            if (m_HistogramEnabled)
            {
                updateCoordsTooltip(ev->position().toPoint());
            }
        }
    }
    else
    {
        // Don't use m_CurrentPointerDragStatus because is only set when the mouse is pressed
        auto nodeSelected = getCurrentNodeSelected(ev->pos(), true);
        if (nodeSelected != NO_NODE_SELECTED)
        {
            setCursor(Qt::CursorShape::PointingHandCursor);
        }
        else
        {
            setCursor(Qt::CursorShape::ArrowCursor);
        }
    }
}

void CurvesWidget::setCurve(const an::ui::ToneCurveModel& curve)
{
    std::lock_guard lock(m_mutex);
    m_curve = curve;

    splineUpdated();
}

an::ui::ToneCurveModel CurvesWidget::getCurve()
{
    std::lock_guard lock(m_mutex);
    return m_curve;
}

bool CurvesWidget::curveIsIdentity()
{
    std::lock_guard lock(m_mutex);
    return m_curve.isIdentity();
}

void CurvesWidget::setCurveInterpolation(an::CurveInterpolation interpolation)
{
    std::lock_guard lock(m_mutex);
    m_curve.setInterpolation(interpolation);
    splineUpdated();
}

an::CurveInterpolation CurvesWidget::getCurveInterpolation()
{
    std::lock_guard lock(m_mutex);
    return m_curve.interpolation();
}

void CurvesWidget::setListener(CurvesWidgetListener* listener)
{
    m_Listener = listener;
}

CurvesWidgetListener* CurvesWidget::getListener() const
{
    return m_Listener;
}

void CurvesWidget::drawSplines(QPainter& painter)
{
    if (m_curve.isEmpty())
        return;

    QPen pen;

    pen.setStyle(Qt::SolidLine);
    pen.setWidthF(1.0);
    pen.setColor(Qt::white);
    /*if (m_Histogram->getNumOfChannels() == 1)
        pen.setColor(Qt::yellow);
    else
        pen.setColor(Qt::white);*/

    painter.setPen(pen);

    // Draw the lines
    for (int i = 1; i < m_spline_to_render.size(); ++i)
    {
        painter.drawLine(m_spline_to_render[i - 1], m_spline_to_render[i]);
    }

    // Draw te nodes
    pen.setWidthF(5);
    painter.setPen(pen);
    for (size_t i = 0; i < m_nodes_to_render.size(); ++i)
    {
        painter.drawPoint(m_nodes_to_render[i]);
    }
}

int CurvesWidget::getCurrentNodeSelected(const QPoint& pos, bool testOnly)
{
    if (m_curve.isEmpty())
    {
        return NO_NODE_SELECTED;
    }

    const double w = (double)width();
    const double h = (double)getPlotHeight();
    const double normX = (double)pos.x() / w;
    const double normY = 1.0 - ((double)pos.y() / h);
    const double tolerance_x = (1.0 / w) * 5.5;
    const double tolerance_y = (1.0 / h) * 5.5;

    // 1) Check if pos is over an existing node, if so, return its index
    int idx = 0;
    const std::vector<an::CurveControlPoint>& nodes = m_curve.points();
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        if (std::fabs(nodes[i].input - normX) <= tolerance_x)
        {
            if (std::fabs(nodes[i].output - normY) <= tolerance_y)
            {
                return idx;
            }
        }

        ++idx;
    }

    // 2) Check if pos is over the spline between two points, if so, create a new node at that position and update the
    // spline, and return its index
    double current_y = m_curve.evaluate(normX);
    if (std::fabs(current_y - normY) <= tolerance_y)
    {
        if (testOnly)
        {
            // Return current number of nodes, to indicate that a new node would be created
            return (int)m_curve.size();
        }

        idx = (int)m_curve.insertPoint(normX, normY);
        splineUpdated();
        return idx;
    }

    return NO_NODE_SELECTED;
}

void CurvesWidget::splineUpdated()
{
    {
        std::lock_guard lock(m_mutex);

        update_spline_to_render();

        if (m_Listener)
        {
            m_Listener->onCurvesUpdated(m_curve);
        }
    }

    update();
}

void CurvesWidget::update_spline_to_render()
{
    std::lock_guard lock(m_mutex);

    // Force updating the gradient area
    if (m_GradientAreaEnabled)
        setGradientAreaEnabled(true);

    int plotHeight = getPlotHeight();
    int origin = 0.0f * width();
    QPoint previousPos(origin, plotHeight);

    m_spline_to_render.clear();
    m_spline_to_render.reserve(width());

    m_spline_to_render.push_back(QPoint(origin, plotHeight));

    for (int i = 0; i < width(); ++i)
    {
        float x = i / (float)width();
        float y = static_cast<float>(m_curve.evaluate(x));
        QPoint newPos(x * width(), plotHeight * (1 - y));

        if (newPos.x() < origin)
            continue;

        m_spline_to_render.push_back(newPos);
    }

    m_nodes_to_render.clear();
    const std::vector<an::CurveControlPoint>& nodes = m_curve.points();
    m_nodes_to_render.reserve(nodes.size());
    for (const an::CurveControlPoint& node : nodes)
    {
        m_nodes_to_render.push_back(QPoint(node.input * width(), plotHeight * (1 - node.output)));
    }
}
