// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageViewScrollArea.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"
#include "ImagePointSelectionController.h"

ImagePointSelectionController::ImagePointSelectionController(ImageViewScrollArea* parent,
                                                             ImagePointSelectorControl* control)
    : SelectionControllerBase(parent), m_Control(control)
{
}

void ImagePointSelectionController::enablePointSelection(bool enable)
{
    setSelectionEnabled(enable);
}

bool ImagePointSelectionController::isPointSelectionEnabled() const
{
    return isSelectionEnabled();
}

void ImagePointSelectionController::onSelectionEnabledChanged(bool enabled)
{
    if (!enabled)
    {
        m_PointSelection.setX(0);
        m_PointSelection.setY(0);
    }
}

void ImagePointSelectionController::updatePointSelection(const QPoint& selection)
{
    m_PointSelection = selection;
    m_ViewControl->getViewWidget()->update();
}

QPoint ImagePointSelectionController::getPointSelection() const
{
    return m_PointSelection;
}

void ImagePointSelectionController::resetPointSelection()
{
    m_PointSelection.setX(0);
    m_PointSelection.setY(0);

    m_ViewControl->getViewWidget()->update();
}

bool ImagePointSelectionController::onMousePressEvent(QMouseEvent* ev)
{
    if (!isListenerEnabled())
        return false;

    if (ev->button() == Qt::LeftButton)
    {
        ev->accept();

        auto mouseAbsPos = ev->pos();
        auto imagePos = m_ParentArea->widget()->pos();
        auto imageScaledArea = m_ViewControl->getViewWidget()->getCurrentScaledImageArea();

        auto mouseRelPos = mouseAbsPos - imagePos;

        if (imageScaledArea.contains(mouseRelPos))
        {
            m_PointSelection = mouseRelPos * (1.0f / m_ViewControl->getViewWidget()->getCurrentScaleFactor());
            m_ViewControl->getViewWidget()->update();

            if (m_Control)
            {
                m_Control->onPointSelected(m_PointSelection);
            }
        }

        return true;
    }
    // else if (ev->button() == Qt::RightButton)
    //{
    //     ev->accept();

    //    auto mouseAbsPos = ev->pos();
    //    auto imagePos = m_ParentArea->widget()->pos();
    //    auto imageScaledArea = m_ViewControl->getViewWidget()->getCurrentScaledImageArea();

    //    auto mouseRelPos = mouseAbsPos - imagePos;

    //    if (imageScaledArea.contains(mouseRelPos))
    //    {
    //        // ??

    //        m_ViewControl->getViewWidget()->update();
    //    }

    //    return true;
    //}

    return false;
}

void ImagePointSelectionController::setDrawCoords(bool enable)
{
    m_DrawCoords = enable;
    m_ViewControl->getViewWidget()->update();
}

bool ImagePointSelectionController::getDrawCoord() const
{
    return m_DrawCoords;
}

void ImagePointSelectionController::onDraw(QPainter& painter)
{
    if (m_PointSelection.x() > 0 && m_PointSelection.y() > 0)
    {
        drawPointSelection(painter);
    }
}

void ImagePointSelectionController::drawPointSelection(QPainter& painter)
{
    const float currentScale = m_ViewControl->getViewWidget()->getCurrentScaleFactor();

    QPen pen = painter.pen();
    pen.setColor(Qt::yellow);
    pen.setWidth(1);
    pen.setCosmetic(true);
    painter.setPen(pen);
    int scaled_x = (int)((float)m_PointSelection.x() * currentScale);
    int scaled_y = (int)((float)m_PointSelection.y() * currentScale);

    painter.drawLine(0, scaled_y, m_ParentArea->widget()->width(), scaled_y);
    painter.drawLine(scaled_x, 0, scaled_x, m_ParentArea->widget()->height());

    if (m_DrawCoords)
    {
        std::string coords = fmt::format("X: {}, Y: {}", m_PointSelection.x(), m_PointSelection.y());

        int pos_x, pos_y;
        if (scaled_x < m_ParentArea->widget()->width() - 100)
        {
            pos_x = scaled_x + 10;
        }
        else
        {
            pos_x = scaled_x - 100;
        }

        if (scaled_y < 40)
        {
            pos_y = scaled_y + 20;
        }
        else
        {
            pos_y = scaled_y - 10;
        }

        painter.drawText(pos_x, pos_y, QString::fromStdString(coords));
    }
}
