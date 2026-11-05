// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageViewScrollArea.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"
#include "ImageCropController.h"

constexpr int SIZE_CURSOR_TOLERANCE_PX = 3;

ImageCropController::ImageCropController(ImageViewScrollArea* parent, ImageAreaSelectorControl* selectorControl)
    : ImageViewScrollAreaListener(parent), m_SelectorControl(selectorControl)
{
    m_ViewControl->getViewWidget()->enableQuickZoom(false);
}

bool ImageCropController::onMousePressEvent(QMouseEvent* ev)
{
    if (!isListenerEnabled())
        return false;

    if (ev->button() == Qt::LeftButton)
    {
        ev->accept();

        m_LastMouseDragLocation = ev->globalPosition().toPoint();
        m_MouseLeftButtonPressed = true;

        auto imagePos = m_ParentArea->widget()->pos();
        auto imageSize = m_ParentArea->widget()->size();
        auto mouseRelPos = ev->pos();

        auto imageArea = m_ParentArea->widget()->rect();
        imageArea.moveLeft(imagePos.x());
        imageArea.moveTop(imagePos.y());

        // New selection started
        if (m_EnableNewSelection)
        {
            if (imageArea.contains(mouseRelPos))
            {
                m_NewSelectionInProgress = true;
                m_ViewControl->getViewWidget()->changeSelectionAreaWidth(1);
                m_ViewControl->getViewWidget()->changeSelectionAreaHeight(1);
                m_ViewControl->getViewWidget()->moveSelectionArea(mouseRelPos.x() - imagePos.x(),
                                                                  mouseRelPos.y() - imagePos.y());
                m_ViewControl->getViewWidget()->enableSelectionArea(true);
                m_ViewControl->getViewWidget()->update();

                m_NewSelectionOriginPoint = QPoint(mouseRelPos.x() - imagePos.x(), mouseRelPos.y() - imagePos.y());
            }
        }
        else if (m_EnableOneClickMove)
        {
            auto selArea = m_ViewControl->getViewWidget()->getCurrentScaledSelectionArea();

            QRect realSelArea(imagePos.x() + selArea.x(), imagePos.y() + selArea.y(), selArea.width(),
                              selArea.height());

            auto currentMousePositionType = getMousePositionType(mouseRelPos, realSelArea);
            if (currentMousePositionType == MousePositionType::outside_pos && imageArea.contains(mouseRelPos))
            {
                int posX = std::clamp((mouseRelPos.x() - imagePos.x()) - selArea.width() / 2, 0,
                                      imageSize.width() - selArea.width());
                int posY = std::clamp((mouseRelPos.y() - imagePos.y()) - selArea.height() / 2, 0,
                                      imageSize.height() - selArea.height());

                m_ViewControl->getViewWidget()->moveSelectionArea(posX, posY);
                m_SelectorControl->selectionAreaUpdated();
            }
        }

        return true;
    }

    return false;
}

bool ImageCropController::onMouseReleaseEvent(QMouseEvent* ev)
{
    if (!isListenerEnabled())
        return false;

    if (ev->button() == Qt::LeftButton)
    {
        ev->accept();

        if (m_NewSelectionInProgress)
        {
            m_EnableNewSelection = false;
            m_NewSelectionInProgress = false;
            m_SelectorControl->newSelectionFinished();
        }
        else
        {
            m_LastMouseDragLocation = ev->globalPosition().toPoint();

            auto mouseRelPos = ev->pos();
            auto selArea = m_ViewControl->getViewWidget()->getCurrentScaledSelectionArea();

            auto imagePos = m_ParentArea->widget()->pos();
            QRect realSelArea(imagePos.x() + selArea.x(), imagePos.y() + selArea.y(), selArea.width(),
                              selArea.height());

            auto currentMousePositionType = getMousePositionType(mouseRelPos, realSelArea);
            if (m_LastMousePositionType != currentMousePositionType)
            {
                changeMouseMode(currentMousePositionType);
                m_LastMousePositionType = currentMousePositionType;
            }

            m_SelectorControl->selectionAreaChanged();
        }

        m_MouseLeftButtonPressed = false;
        return true;
    }

    return false;
}

bool ImageCropController::onMouseMoveEvent(QMouseEvent* ev)
{
    if (!isListenerEnabled())
        return false;

    auto mouseAbsPos = ev->globalPosition().toPoint();
    auto mouseRelPos = ev->pos();

    auto imagePos = m_ParentArea->widget()->pos();

    auto selArea = m_ViewControl->getViewWidget()->getCurrentScaledSelectionArea();

    QRect realSelArea(imagePos.x() + selArea.x(), imagePos.y() + selArea.y(), selArea.width(), selArea.height());

    if (m_MouseLeftButtonPressed)
    {
        ev->accept();

        QPoint delta = mouseAbsPos - m_LastMouseDragLocation;
        m_LastMouseDragLocation = mouseAbsPos;

        if (m_NewSelectionInProgress)
        {
            int xpos = mouseRelPos.x() - imagePos.x();
            int ypos = mouseRelPos.y() - imagePos.y();

            if (xpos > m_NewSelectionOriginPoint.x())
            {
                m_ViewControl->getViewWidget()->changeSelectionAreaX(m_NewSelectionOriginPoint.x());
                m_ViewControl->getViewWidget()->changeSelectionAreaWidth(xpos - m_NewSelectionOriginPoint.x());
            }
            else
            {
                m_ViewControl->getViewWidget()->changeSelectionAreaX(xpos);
                m_ViewControl->getViewWidget()->changeSelectionAreaWidth(m_NewSelectionOriginPoint.x() - xpos);
            }

            if (ypos > m_NewSelectionOriginPoint.y())
            {
                m_ViewControl->getViewWidget()->changeSelectionAreaY(m_NewSelectionOriginPoint.y());
                m_ViewControl->getViewWidget()->changeSelectionAreaHeight(ypos - m_NewSelectionOriginPoint.y());
            }
            else
            {
                m_ViewControl->getViewWidget()->changeSelectionAreaY(ypos);
                m_ViewControl->getViewWidget()->changeSelectionAreaHeight(m_NewSelectionOriginPoint.y() - ypos);
            }

            m_SelectorControl->selectionAreaUpdated();
        }
        else
        {
            auto selAreaScaledSize = m_ViewControl->getViewWidget()->getCurrentScaledSelectionArea();

            if (m_LastMousePositionType == MousePositionType::inside_pos)
            {
                m_ViewControl->getViewWidget()->moveSelectionArea(selAreaScaledSize.x() + delta.x(),
                                                                  selAreaScaledSize.y() + delta.y());
                m_SelectorControl->selectionAreaUpdated();
            }
            else if (m_LastMousePositionType == MousePositionType::left_side_pos)
            {
                m_ViewControl->getViewWidget()->changeSelectionAreaX(selAreaScaledSize.x() + delta.x());
                m_SelectorControl->selectionAreaUpdated();
            }
            else if (m_LastMousePositionType == MousePositionType::top_side_pos)
            {
                m_ViewControl->getViewWidget()->changeSelectionAreaY(selAreaScaledSize.y() + delta.y());
                m_SelectorControl->selectionAreaUpdated();
            }
            else if (m_LastMousePositionType == MousePositionType::right_side_pos)
            {
                m_ViewControl->getViewWidget()->changeSelectionAreaWidth(selAreaScaledSize.width() + delta.x());
                m_SelectorControl->selectionAreaUpdated();
            }
            else if (m_LastMousePositionType == MousePositionType::bottom_side_pos)
            {
                m_ViewControl->getViewWidget()->changeSelectionAreaHeight(selAreaScaledSize.height() + delta.y());
                m_SelectorControl->selectionAreaUpdated();
            }
            else if (m_LastMousePositionType == MousePositionType::top_left_corner_pos)
            {
                m_ViewControl->getViewWidget()->changeSelectionAreaX(selAreaScaledSize.x() + delta.x());
                m_ViewControl->getViewWidget()->changeSelectionAreaY(selAreaScaledSize.y() + delta.y());
                m_SelectorControl->selectionAreaUpdated();
            }
            else if (m_LastMousePositionType == MousePositionType::top_right_corner_pos)
            {
                m_ViewControl->getViewWidget()->changeSelectionAreaWidth(selAreaScaledSize.width() + delta.x());
                m_ViewControl->getViewWidget()->changeSelectionAreaY(selAreaScaledSize.y() + delta.y());
                m_SelectorControl->selectionAreaUpdated();
            }
            else if (m_LastMousePositionType == MousePositionType::bottom_left_corner_pos)
            {
                m_ViewControl->getViewWidget()->changeSelectionAreaX(selAreaScaledSize.x() + delta.x());
                m_ViewControl->getViewWidget()->changeSelectionAreaHeight(selAreaScaledSize.height() + delta.y());
                m_SelectorControl->selectionAreaUpdated();
            }
            else if (m_LastMousePositionType == MousePositionType::bottom_right_corner_pos)
            {
                m_ViewControl->getViewWidget()->changeSelectionAreaWidth(selAreaScaledSize.width() + delta.x());
                m_ViewControl->getViewWidget()->changeSelectionAreaHeight(selAreaScaledSize.height() + delta.y());
                m_SelectorControl->selectionAreaUpdated();
            }
        }

        return true;
    }
    else if (!m_EnableNewSelection)
    {
        auto currentMousePositionType = getMousePositionType(mouseRelPos, realSelArea);
        if (m_LastMousePositionType != currentMousePositionType)
        {
            changeMouseMode(currentMousePositionType);
            m_LastMousePositionType = currentMousePositionType;
        }
    }

    return false;
}

void ImageCropController::setListenerEnabled(const bool enabled)
{
    ImageViewScrollAreaListener::setListenerEnabled(enabled);

    m_ViewControl->getViewWidget()->enableSelectionArea(enabled);
    m_ViewControl->getViewWidget()->update();
    m_ParentArea->setMouseTracking(enabled);
    m_ViewControl->getViewWidget()->setMouseTracking(enabled);
}

void ImageCropController::startNewSelection()
{
    m_EnableNewSelection = true;
    m_ViewControl->getViewWidget()->enableSelectionArea(false);
    m_ViewControl->getViewWidget()->update();

    changeMouseMode(MousePositionType::outside_pos);
}

void ImageCropController::cancelNewSelection()
{
    m_EnableNewSelection = false;
    m_ViewControl->getViewWidget()->enableSelectionArea(true);
}

void ImageCropController::enableOneClickMove(bool enable)
{
    m_EnableOneClickMove = enable;
}

bool ImageCropController::isOneClickMoveEnabled() const
{
    return m_EnableOneClickMove;
}

void ImageCropController::updateSelectionArea(const QRect& area)
{
    m_ViewControl->getViewWidget()->updateSelectionArea(area);
    m_ViewControl->getViewWidget()->update();
}

void ImageCropController::destroy()
{
    changeMouseMode(MousePositionType::outside_pos);
    // Do not disable mouse tracking here — ImageViewControl manages it
    // based on whether the image has WCS data (for celestial coordinate display).
    m_ViewControl->getViewWidget()->enableSelectionArea(false);
    m_ViewControl->getViewWidget()->update();
}

ImageCropController::~ImageCropController()
{
    if (m_ViewControl)
        m_ViewControl->getViewWidget()->enableQuickZoom(true);
}

bool is_equal(int a, int b, int tolerance)
{
    return a >= b - tolerance && a <= b + tolerance;
}

bool is_higher_equal_than(int a, int b, int tolerance)
{
    return a >= b - tolerance;
}

bool is_less_equal_than(int a, int b, int tolerance)
{
    return a <= b + tolerance;
}

MousePositionType ImageCropController::getMousePositionType(const QPoint& mousePos, const QRect& imageArea)
{
    int abosoluteWidth = imageArea.x() + imageArea.width();
    int abosoluteHeight = imageArea.y() + imageArea.height();

    bool insideX = false;
    bool insideY = false;

    if (is_higher_equal_than(mousePos.x(), imageArea.x(), SIZE_CURSOR_TOLERANCE_PX) &&
        is_less_equal_than(mousePos.x(), abosoluteWidth, SIZE_CURSOR_TOLERANCE_PX))
    {
        insideX = true;
    }

    if (is_higher_equal_than(mousePos.y(), imageArea.y(), SIZE_CURSOR_TOLERANCE_PX) &&
        is_less_equal_than(mousePos.y(), abosoluteHeight, SIZE_CURSOR_TOLERANCE_PX))
    {
        insideY = true;
    }

    if (insideX && insideY)
    {
        bool isInLeftSide = is_equal(mousePos.x(), imageArea.x(), SIZE_CURSOR_TOLERANCE_PX);
        bool isInRightSide = is_equal(mousePos.x(), abosoluteWidth, SIZE_CURSOR_TOLERANCE_PX);
        bool isInTopSide = is_equal(mousePos.y(), imageArea.y(), SIZE_CURSOR_TOLERANCE_PX);
        bool isInBottomSide = is_equal(mousePos.y(), abosoluteHeight, SIZE_CURSOR_TOLERANCE_PX);

        if (isInLeftSide)
        {
            if (isInTopSide)
                return MousePositionType::top_left_corner_pos;
            else if (isInBottomSide)
                return MousePositionType::bottom_left_corner_pos;

            return MousePositionType::left_side_pos;
        }
        else if (isInRightSide)
        {
            if (isInTopSide)
                return MousePositionType::top_right_corner_pos;
            else if (isInBottomSide)
                return MousePositionType::bottom_right_corner_pos;

            return MousePositionType::right_side_pos;
        }
        else if (isInTopSide)
        {
            return MousePositionType::top_side_pos;
        }
        else if (isInBottomSide)
        {
            return MousePositionType::bottom_side_pos;
        }

        return MousePositionType::inside_pos;
    }

    return MousePositionType::outside_pos;
}

void ImageCropController::changeMouseMode(MousePositionType newMode)
{
    if (newMode == MousePositionType::outside_pos)
    {
        m_ParentArea->restoreDefaultCursor();
    }
    else if (newMode == MousePositionType::inside_pos)
    {
        m_ParentArea->setCursor(Qt::CursorShape::SizeAllCursor);
    }
    else if (newMode == MousePositionType::left_side_pos || newMode == MousePositionType::right_side_pos)
    {
        m_ParentArea->setCursor(Qt::CursorShape::SizeHorCursor);
    }
    else if (newMode == MousePositionType::top_side_pos || newMode == MousePositionType::bottom_side_pos)
    {
        m_ParentArea->setCursor(Qt::CursorShape::SizeVerCursor);
    }
    else if (newMode == MousePositionType::top_left_corner_pos || newMode == MousePositionType::bottom_right_corner_pos)
    {
        m_ParentArea->setCursor(Qt::CursorShape::SizeFDiagCursor);
    }
    else if (newMode == MousePositionType::top_right_corner_pos || newMode == MousePositionType::bottom_left_corner_pos)
    {
        m_ParentArea->setCursor(Qt::CursorShape::SizeBDiagCursor);
    }
}
