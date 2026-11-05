// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "ImageViewScrollArea.h"

class ImageZoomAndPanController : public ImageViewScrollAreaListener
{
public:
    using ImageViewScrollAreaListener::ImageViewScrollAreaListener;

    bool onWheelEvent(QWheelEvent* ev) override;
    bool onMousePressEvent(QMouseEvent* ev) override;
    bool onMouseReleaseEvent(QMouseEvent* ev) override;
    bool onMouseMoveEvent(QMouseEvent* ev) override;

    /**
     * @brief Refresh cached zoom settings from the database
     *
     * Call this when settings change to update the cached values.
     */
    void refreshZoomSettings();

    virtual ~ImageZoomAndPanController();

protected:
private:
    QPoint m_LastMouseDragLocation = QPoint(0, 0);
    bool m_MouseMiddleButtonPressed = false;

    // Cached zoom settings to avoid SQLite queries on every event
    bool m_CachedZoomRequiresCtrl = false;
    int m_CachedZoomSensitivity = 50;
    bool m_SettingsLoaded = false;
};
