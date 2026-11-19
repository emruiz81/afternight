// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The shapes an owner - an extension's session - draws over a view: data the view's shape overlay draws, in the
/// pixels of the image they lie over, and nothing else reads. They are in no saved image, no project and no annotated
/// picture.
#pragma once

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <algorithm>
#include <vector>

namespace an::ui
{

/// One shape over an image: a closed polygon or an open line of points, its colour, whether it is filled, and a label
/// drawn beside it.
struct ViewShape
{
    /// Its points in the image's pixels, (0, 0) the image's top-left corner; at least one.
    std::vector<QPointF> points;

    /// A polygon whose last point joins its first; an open line otherwise.
    bool closed = false;

    /// Whether a polygon is filled with its colour.
    bool filled = false;

    /// Its colour as the extension stated it; drawn as Night shows a colour (`colorAsShown`).
    QColor color;

    /// Drawn beside the shape; empty for none.
    QString label;

    /// The rectangle its points span, in the image's pixels: what a paint compares with the part it paints.
    QRectF bounds;
};

/// The shapes one owner draws over a view, in the order they are drawn.
struct ViewShapes
{
    std::vector<ViewShape> shapes;
};

/// The rectangle a list of points spans; empty for no point.
[[nodiscard]] inline QRectF boundsOfPoints(const std::vector<QPointF>& points)
{
    if (points.empty())
    {
        return {};
    }
    double left = points.front().x();
    double right = left;
    double top = points.front().y();
    double bottom = top;
    for (const QPointF& point : points)
    {
        left = (std::min)(left, point.x());
        right = (std::max)(right, point.x());
        top = (std::min)(top, point.y());
        bottom = (std::max)(bottom, point.y());
    }
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

} // namespace an::ui
