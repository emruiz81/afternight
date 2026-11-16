// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A rectangle of an image marked on a view as a person marks it: the view at 100 %, the mouse pressed at the
/// rectangle's corner, dragged and released at the opposite one, each event sent to the view's viewport, where a
/// process's selection controller hears it.

#include "controls/ImageViewControl.h"
#include "controls/ImageViewScrollArea.h"
#include "widgets/ImageViewWidget.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QPoint>
#include <QRect>

#include <cmath>

namespace an::tests::processes::shared
{

/// Sends one mouse event of the left button to a widget at a position of its own.
inline void sendLeftMouse(QWidget& widget, const QEvent::Type type, const QPoint position)
{
    const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    const Qt::MouseButtons held = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent event(type, QPointF(position), QPointF(widget.mapToGlobal(position)), button, held, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &event);
}

/// Where a point of the image a view shows lies in its viewport now.
[[nodiscard]] inline QPoint viewportPointOf(ImageViewControl& view, const QPoint image)
{
    QWidget* const viewport = view.getScrollArea()->viewport();
    ImageViewWidget* const widget = view.getViewWidget();
    const double scale = widget->getCurrentScaleFactor();
    return widget->mapTo(viewport, QPoint(static_cast<int>(std::lround(image.x() * scale)),
                                          static_cast<int>(std::lround(image.y() * scale))));
}

/// Shows a view's image at 100 %, so a point of the image is a point of the viewport, offset.
inline void showAtFullSize(ImageViewControl& view)
{
    view.zoom100();
    QCoreApplication::processEvents();
}

/// Drags a new selection of a rectangle of the image on a view, at 100 %: pressed at its top-left corner, released at
/// its bottom-right one, as a person draws a new selection once a process's button asks for one.
inline void dragNewSelection(ImageViewControl& view, const QRect& image)
{
    showAtFullSize(view);
    QWidget* const viewport = view.getScrollArea()->viewport();
    const QPoint from = viewportPointOf(view, image.topLeft());
    const QPoint to = viewportPointOf(view, QPoint(image.x() + image.width(), image.y() + image.height()));
    sendLeftMouse(*viewport, QEvent::MouseButtonPress, from);
    sendLeftMouse(*viewport, QEvent::MouseMove, (from + to) / 2);
    sendLeftMouse(*viewport, QEvent::MouseMove, to);
    sendLeftMouse(*viewport, QEvent::MouseButtonRelease, to);
    QCoreApplication::processEvents();
}

/// Drags a selection already drawn on a view by an offset of the image's pixels, at 100 %: pressed inside it, at its
/// centre, moved and released.
inline void dragSelectionBy(ImageViewControl& view, const QPoint offset)
{
    showAtFullSize(view);
    QWidget* const viewport = view.getScrollArea()->viewport();
    const QRect selection = view.getViewWidget()->getCurrentSelectionArea();
    const QPoint from = viewportPointOf(view, selection.center());
    const QPoint to = viewportPointOf(view, selection.center() + offset);
    // A move with no button first, as the pointer comes to the selection: it tells the controller it is inside.
    sendLeftMouse(*viewport, QEvent::MouseMove, from);
    sendLeftMouse(*viewport, QEvent::MouseButtonPress, from);
    sendLeftMouse(*viewport, QEvent::MouseMove, to);
    sendLeftMouse(*viewport, QEvent::MouseButtonRelease, to);
    QCoreApplication::processEvents();
}

} // namespace an::tests::processes::shared
