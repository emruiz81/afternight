// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageViewScrollArea.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"
#include "ImageZoomAndPanController.h"
#include "controls/IAppContext.h"
#include "controls/settings/SettingsKeys.h"
#include "controls/settings/SettingsDefaults.h"
#include "widgets/ZoomManager.h"

#include <cmath>

namespace
{

[[nodiscard]] an::ui::IGlobalSettingsProvider* currentGlobalSettings()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getGlobalSettings();
    }

    return nullptr;
}

} // namespace

void ImageZoomAndPanController::refreshZoomSettings()
{
    auto* settings = currentGlobalSettings();

    if (settings)
    {
        QString requiresCtrlStr = settings->select(SettingsKeys::INTERFACE_ZOOM_REQUIRES_CTRL,
                                                   SettingsDefaults::INTERFACE_ZOOM_REQUIRES_CTRL ? "1" : "0");
        m_CachedZoomRequiresCtrl = (requiresCtrlStr == "1");

        QString sensitivityStr = settings->select(SettingsKeys::INTERFACE_ZOOM_SENSITIVITY,
                                                  QString::number(SettingsDefaults::INTERFACE_ZOOM_SENSITIVITY));
        m_CachedZoomSensitivity = qBound(1, sensitivityStr.toInt(), 100);
    }
    else
    {
        m_CachedZoomRequiresCtrl = SettingsDefaults::INTERFACE_ZOOM_REQUIRES_CTRL;
        m_CachedZoomSensitivity = SettingsDefaults::INTERFACE_ZOOM_SENSITIVITY;
    }

    m_SettingsLoaded = true;
}

bool ImageZoomAndPanController::onWheelEvent(QWheelEvent* ev)
{
    if (!isListenerEnabled())
        return false;

    // Load settings on first use (lazy initialization)
    if (!m_SettingsLoaded)
    {
        refreshZoomSettings();
    }

    // Check if we should zoom based on Ctrl key requirement
    bool ctrlPressed = (ev->modifiers() & Qt::ControlModifier);
    bool shouldZoom = m_CachedZoomRequiresCtrl ? ctrlPressed : true;

    // Zoom control
    if (shouldZoom)
    {
        ev->accept();

        auto delta = ev->angleDelta();

        // Calculate zoom factor based on sensitivity (1-100)
        // Sensitivity of 50 gives the original 1.25/0.8 factors
        // Lower sensitivity = smaller factor changes, higher = larger
        float baseFactor = 1.0f + (m_CachedZoomSensitivity / 100.0f) * 0.5f; // Range: 1.005 to 1.5

        float factor;
        if (delta.y() > 0)
        {
            factor = baseFactor;
        }
        else
        {
            factor = 1.0f / baseFactor;
        }

        auto hBarPos = m_ParentArea->horizontalScrollBar()->value();
        auto vBarPos = m_ParentArea->verticalScrollBar()->value();

        auto pos = ev->position();
        const double offsetx = static_cast<double>(hBarPos) + pos.x();
        const double offsety = static_cast<double>(vBarPos) + pos.y();
        const double appliedFactor = an::ui::ZoomManager::effectiveRelativeZoomFactor(
            m_ViewControl->getViewWidget()->getCurrentScaleFactor(), factor);

        m_ViewControl->getViewWidget()->scaleImageView(factor);

        if (std::abs(appliedFactor - 1.0) > 1.0e-12)
        {
            m_ParentArea->horizontalScrollBar()->setValue(
                static_cast<int>(std::lround(offsetx * appliedFactor - pos.x())));
            m_ParentArea->verticalScrollBar()->setValue(
                static_cast<int>(std::lround(offsety * appliedFactor - pos.y())));
        }

        return true;
    }

    return false;
}

bool ImageZoomAndPanController::onMousePressEvent(QMouseEvent* ev)
{
    if (!isListenerEnabled())
        return false;

    if (ev->button() == Qt::MiddleButton)
    {
        ev->accept();

        m_LastMouseDragLocation = ev->globalPosition().toPoint();
        m_MouseMiddleButtonPressed = true;
        m_ParentArea->setCursor(Qt::CursorShape::OpenHandCursor);
        return true;
    }

    return false;
}

bool ImageZoomAndPanController::onMouseReleaseEvent(QMouseEvent* ev)
{
    if (!isListenerEnabled())
        return false;

    if (ev->button() == Qt::MiddleButton)
    {
        ev->accept();

        m_LastMouseDragLocation = ev->globalPosition().toPoint();
        m_MouseMiddleButtonPressed = false;
        m_ParentArea->restoreDefaultCursor();
        return true;
    }

    return false;
}

bool ImageZoomAndPanController::onMouseMoveEvent(QMouseEvent* ev)
{
    if (!isListenerEnabled())
        return false;

    if (m_MouseMiddleButtonPressed)
    {
        ev->accept();

        QPoint delta = ev->globalPosition().toPoint() - m_LastMouseDragLocation;

        m_ParentArea->horizontalScrollBar()->setValue(m_ParentArea->horizontalScrollBar()->value() - delta.x());
        m_ParentArea->verticalScrollBar()->setValue(m_ParentArea->verticalScrollBar()->value() - delta.y());
        m_LastMouseDragLocation = ev->globalPosition().toPoint();
        return true;
    }

    return false;
}

ImageZoomAndPanController::~ImageZoomAndPanController() {}
