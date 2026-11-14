// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "widgets/ComparisonDisplayModule.h"

#include <algorithm>

#include <QPainter>

#include "theme/AnDesignTokens.h"
#include "theme/NightImage.h"
#include "widgets/ImageViewWidget.h"

namespace an::ui
{

void ComparisonDisplayModule::setOverlayImage(const QImage& image)
{
    m_overlayImage = image;
    requestUpdate();
}

void ComparisonDisplayModule::clearOverlayImage()
{
    if (m_overlayImage.isNull())
    {
        return;
    }

    m_overlayImage = QImage();
    requestUpdate();
}

bool ComparisonDisplayModule::hasOverlayImage() const
{
    return !m_overlayImage.isNull();
}

void ComparisonDisplayModule::setOverlayOpacity(double opacity)
{
    m_overlayOpacity = std::clamp(opacity, 0.0, 1.0);
    requestUpdate();
}

double ComparisonDisplayModule::overlayOpacity() const
{
    return m_overlayOpacity;
}

void ComparisonDisplayModule::onDraw(QPainter& painter)
{
    if (!hasOverlayImage() || !m_ViewWidget)
    {
        return;
    }

    const QRect targetRect = m_ViewWidget->getCurrentScaledImageArea();
    if (!targetRect.isValid() || targetRect.isEmpty())
    {
        return;
    }

    // Under Night the overlay's Night form, made off the GUI thread: nothing until it is, and the view painted again
    // then.
    ImageViewWidget* const view = m_ViewWidget;
    const QImage overlay =
        isNightMode() ? nightImage(m_overlayImage, view, [view]() { view->update(); }) : m_overlayImage;
    if (overlay.isNull())
    {
        return;
    }
    painter.save();
    painter.setOpacity(m_overlayOpacity);
    painter.drawImage(targetRect, overlay);
    painter.restore();
}

void ComparisonDisplayModule::requestUpdate()
{
    if (m_ViewWidget)
    {
        m_ViewWidget->update();
    }
}

} // namespace an::ui
