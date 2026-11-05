// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageViewScrollArea.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"
#include "ImageSampleSelectionController.h"

ImageSampleSelectionController::ImageSampleSelectionController(ImageViewScrollArea* parent,
                                                               ImageSampleSelectorControl* control)
    : SelectionControllerBase(parent), m_Control(control)
{
}

void ImageSampleSelectionController::enableSampleSelection(bool enable)
{
    setSelectionEnabled(enable);
}

bool ImageSampleSelectionController::isSampleSelectionEnabled() const
{
    return isSelectionEnabled();
}

void ImageSampleSelectionController::onSelectionEnabledChanged(bool enabled)
{
    if (!enabled)
    {
        m_ImageSampleSelectionList->clear();
    }
}

void ImageSampleSelectionController::updateSampleSelection(const ImageSampleSelectionListSPtr& sampleSelection)
{
    m_ImageSampleSelectionList = sampleSelection;
    if (!m_ImageSampleSelectionList)
    {
        m_ImageSampleSelectionList = std::make_shared<ImageSampleSelectionList>();
    }

    m_ViewControl->getViewWidget()->update();
}

ImageSampleSelectionListSPtr ImageSampleSelectionController::getSampleSelection() const
{
    return m_ImageSampleSelectionList;
}

void ImageSampleSelectionController::resetSampleSelection()
{
    m_ImageSampleSelectionList->clear();

    m_ViewControl->getViewWidget()->update();
}

bool ImageSampleSelectionController::onMousePressEvent(QMouseEvent* ev)
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
            // First check if an area already exists at the given pos
            for (auto& sample : *m_ImageSampleSelectionList)
            {
                // Ignore if is in the exact position of an existing sample
                // TODO: Give some tolerance from the center
                if (sample.pos == mouseRelPos)
                {
                    return true;
                }
            }

            ImageSampleSelection sample;

            sample.pos = mouseRelPos * (1.0f / m_ViewControl->getViewWidget()->getCurrentScaleFactor());
            sample.size = m_Control->getSampleRadius();

            m_ImageSampleSelectionList->push_back(sample);

            m_ViewControl->getViewWidget()->update();
            m_Control->addSampleSelection(sample);
        }

        return true;
    }
    else if (ev->button() == Qt::RightButton)
    {
        ev->accept();

        auto mouseAbsPos = ev->pos();
        auto imagePos = m_ParentArea->widget()->pos();
        auto imageScaledArea = m_ViewControl->getViewWidget()->getCurrentScaledImageArea();

        auto mouseRelPos = mouseAbsPos - imagePos;

        if (imageScaledArea.contains(mouseRelPos))
        {
            for (auto& sample : *m_ImageSampleSelectionList)
            {
                if (sample.scaledArea.contains(mouseRelPos))
                {
                    m_Control->removeSampleSelection(sample);
                    m_ImageSampleSelectionList->remove(sample);
                    break;
                }
            }

            m_ViewControl->getViewWidget()->update();
        }

        return true;
    }

    return false;
}

void ImageSampleSelectionController::onDraw(QPainter& painter)
{
    if (m_ImageSampleSelectionList && !m_ImageSampleSelectionList->empty())
    {
        drawSampleSelection(painter);
    }
}

void ImageSampleSelectionController::drawSampleSelection(QPainter& painter)
{
    const float sample_size = 15.0f;
    const float currentScale = m_ViewControl->getViewWidget()->getCurrentScaleFactor();
    for (auto& sample : *m_ImageSampleSelectionList)
    {
        QPen pen = painter.pen();
        pen.setColor(Qt::yellow);
        pen.setWidth(1);
        pen.setCosmetic(true);
        painter.setPen(pen);

        int scaled_x = (int)((float)sample.pos.x() * currentScale);
        int scaled_y = (int)((float)sample.pos.y() * currentScale);
        // Don't use the real sample size to render the area, it might be too big over the screen
        // int scaled_size = (int)((float)sample.size * currentScale);
        int scaled_size = (int)(sample_size * currentScale);

        sample.scaledArea = QRect(scaled_x - scaled_size, scaled_y - scaled_size, scaled_size * 2, scaled_size * 2);

        painter.drawRect(sample.scaledArea);
    }
}
