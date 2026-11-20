// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/WindowPlacement.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QWindow>

#include <algorithm>

namespace an::ui
{

QRect geometryWithinArea(const QRect& geometry, const QMargins& frame, const QSize& least, const QRect& area)
{
    // The room the area leaves the window's own rectangle once its frame is taken out.
    const QRect room = area.marginsRemoved(frame);
    if (room.width() < least.width() || room.height() < least.height())
    {
        return geometry;
    }
    const QSize size = geometry.size().boundedTo(room.size()).expandedTo(least);
    // Moved in only where it stands out: the left and top edges kept where the window still fits.
    const int left = std::clamp(geometry.left(), room.left(), room.right() + 1 - size.width());
    const int top = std::clamp(geometry.top(), room.top(), room.bottom() + 1 - size.height());
    return QRect(QPoint(left, top), size);
}

void holdWithinAvailableArea(QWidget& window)
{
    const QScreen* screen = window.screen();
    if (screen == nullptr)
    {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr)
    {
        return;
    }
    // The frame the window system draws around the window: known once its native window exists.
    QMargins frame;
    if (const QWindow* const native = window.windowHandle())
    {
        frame = native->frameMargins();
    }
    const QRect held = geometryWithinArea(window.geometry(), frame, window.minimumSize(), screen->availableGeometry());
    if (held != window.geometry())
    {
        window.setGeometry(held);
    }
}

} // namespace an::ui
