// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageViewScrollArea.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"
#include "ImageStarSelectionController.h"
#include "theme/PainterGuard.h"

#include <cmath>

namespace
{

/// The box a click selects a star by: two full widths from its centre along each axis, scaled to the view, a width
/// that is not positive taken as one pixel.
[[nodiscard]] QRectF selectionBox(const an::DetectedStar& star, const double scale)
{
    const double fullWidthX = star.psf.fullWidthX > 0.0 ? star.psf.fullWidthX : 1.0;
    const double fullWidthY = star.psf.fullWidthY > 0.0 ? star.psf.fullWidthY : 1.0;
    const double halfWidth = fullWidthX * scale * 2.0;
    const double halfHeight = fullWidthY * scale * 2.0;
    return QRectF(star.positionX * scale - halfWidth, star.positionY * scale - halfHeight, halfWidth * 2.0,
                  halfHeight * 2.0);
}

} // namespace

ImageStarSelectionController::ImageStarSelectionController(ImageViewScrollArea* parent,
                                                           ImageStarSelectorControl* control)
    : ImageViewScrollAreaListener(parent), m_Control(control)
{
    m_ViewControl->getViewWidget()->enableQuickZoom(false);
}

std::optional<size_t> ImageStarSelectionController::starAt(const QPoint& widgetPosition) const
{
    if (!m_StarSelection || m_ViewWidget == nullptr)
        return std::nullopt;

    const double scale = m_ViewWidget->getCurrentScaleFactor();
    const QPointF position(widgetPosition);
    for (size_t idx = 0; idx < m_StarSelection->size(); ++idx)
    {
        if (selectionBox((*m_StarSelection)[idx], scale).contains(position))
        {
            return idx;
        }
    }
    return std::nullopt;
}

bool ImageStarSelectionController::onMousePressEvent(QMouseEvent* ev)
{
    if (!isListenerEnabled())
        return false;

    if (!m_InputCaptureEnabled)
        return false;

    if (ev->button() == Qt::LeftButton)
    {
        ev->accept();

        auto mouseAbsPos = ev->pos();
        auto imagePos = m_ParentArea->widget()->pos();
        auto imageScaledArea = m_ViewWidget->getCurrentScaledImageArea();

        auto mouseRelPos = mouseAbsPos - imagePos;

        if (imageScaledArea.contains(mouseRelPos))
        {
            if (const std::optional<size_t> idx = starAt(mouseRelPos); idx.has_value())
            {
                m_Control->onStarSelected(*idx);
                return true;
            }
        }

        return false;
    }
    else if (ev->button() == Qt::RightButton)
    {
        ev->accept();
        return true;
    }

    return false;
}

void ImageStarSelectionController::setListenerEnabled(const bool enabled)
{
    ImageViewScrollAreaListener::setListenerEnabled(enabled);

    enableStarSelection(enabled);
}

void ImageStarSelectionController::destroy()
{
    enableStarSelection(false);
}

void ImageStarSelectionController::enableStarSelection(bool enable)
{
    m_DisplayModuleEnabled = enable;
    if (!m_DisplayModuleEnabled)
    {
        m_StarSelection = std::make_shared<const an::ui::StarList>();

        resetHighlightStarSelection();
        resetStarSelectionScaleFactor();
    }

    if (m_ViewWidget)
        m_ViewWidget->update();
}

bool ImageStarSelectionController::isStarSelectionEnabled() const
{
    return m_DisplayModuleEnabled;
}

void ImageStarSelectionController::updateStarSelection(const an::ui::StarListPtr& starSelection)
{
    m_StarSelection = starSelection ? starSelection : std::make_shared<const an::ui::StarList>();

    resetHighlightStarSelection();

    if (m_ViewWidget)
        m_ViewWidget->update();
}

void ImageStarSelectionController::setHighlightStarSelection(const QPointF& center)
{
    m_HighlightStarSelection = center;

    if (m_ViewWidget)
        m_ViewWidget->update();
}

QPointF ImageStarSelectionController::getHighlightStarSelection() const
{
    return m_HighlightStarSelection;
}

void ImageStarSelectionController::resetHighlightStarSelection()
{
    setHighlightStarSelection(QPoint(0, 0));
}

void ImageStarSelectionController::setStarSelectionScaleFactor(float factor)
{
    m_StarSelectionScaleFactor = factor;

    if (m_ViewWidget)
        m_ViewWidget->update();
}

void ImageStarSelectionController::resetStarSelectionScaleFactor()
{
    setStarSelectionScaleFactor(1.0f);
}

float ImageStarSelectionController::getStarSelectionScaleFactor() const
{
    return m_StarSelectionScaleFactor;
}

an::ui::StarListPtr ImageStarSelectionController::getStarSelection() const
{
    return m_StarSelection;
}

void ImageStarSelectionController::resetStarSelection()
{
    m_StarSelection = std::make_shared<const an::ui::StarList>();

    resetHighlightStarSelection();

    if (m_ViewWidget)
        m_ViewWidget->update();
}

void ImageStarSelectionController::setInputCaptureEnabled(bool enabled)
{
    m_InputCaptureEnabled = enabled;
}

bool ImageStarSelectionController::isInputCaptureEnabled() const
{
    return m_InputCaptureEnabled;
}

ImageStarSelectionController::~ImageStarSelectionController()
{
    if (m_ViewControl)
        m_ViewControl->getViewWidget()->enableQuickZoom(true);
}

void ImageStarSelectionController::onDraw(QPainter& painter)
{
    drawStarSelection(painter);

    if (m_HighlightStarSelection.x() > 0 && m_HighlightStarSelection.y() > 0)
    {
        drawHighlightStarSelection(painter);
    }
}

void ImageStarSelectionController::drawStarSelection(QPainter& painter)
{
    if (!m_StarSelection)
        return;

    const double currentScale = m_ViewWidget->getCurrentScaleFactor();

    for (const an::DetectedStar& star : *m_StarSelection)
    {
        QPen pen = painter.pen();
        if (star.psf.saturated)
            pen.setColor(Qt::magenta);
        else
            pen.setColor(Qt::yellow);

        pen.setWidth(1);
        pen.setCosmetic(true);
        painter.setPen(pen);

        // The fitted ellipse: two major full widths across, its axis ratio tall, turned by its position angle, centred
        // on the star's engine pixel coordinate.
        const double size = (star.psf.fullWidthX * 2.0 * currentScale) * m_StarSelectionScaleFactor;

        an::ui::PainterGuard painterGuard(&painter);
        painter.translate(star.positionX * currentScale, star.positionY * currentScale);
        painter.rotate(90.0 - star.psf.angleDegrees);
        const double r = star.psf.fullWidthX > 0.0 ? star.psf.fullWidthY / star.psf.fullWidthX : 1.0;
        painter.scale(r, 1);
        painter.drawEllipse(QPointF(0, 0), size, size);
    }
}

void ImageStarSelectionController::drawHighlightStarSelection(QPainter& painter)
{
    QPen pen = painter.pen();
    pen.setColor(Qt::red);
    pen.setWidth(1);
    pen.setCosmetic(true);
    painter.setPen(pen);

    float currentScale = m_ViewWidget->getCurrentScaleFactor();

    int scaled_x = std::ceil(m_HighlightStarSelection.x() * currentScale);
    int scaled_y = std::ceil(m_HighlightStarSelection.y() * currentScale);

    auto area = m_ViewWidget->getCurrentScaledImageArea();
    painter.drawLine(0, scaled_y, area.width(), scaled_y);
    painter.drawLine(scaled_x, 0, scaled_x, area.height());
}
