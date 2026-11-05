// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where the quick-zoom lens sits and which region of the image it magnifies. What it shows of that region and the
/// values it reads out are the presentation's, in `widgets/ViewPresentation.h`.
#pragma once

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>

#include <algorithm>

namespace an::ui
{

/// The region a lens magnifies and the image position under the cursor.
struct QuickZoomRegion
{
    /// The region, in image pixels.
    QRect cropRect;

    /// The image position under the cursor, in image pixels.
    QPointF absolutePosition;
};

/// Where the lens goes beside the cursor, kept inside its owner.
///
/// @param cursorInOwner the cursor, in the owner's coordinates.
/// @param lensSize the lens's size.
/// @param ownerSize the owner's size.
/// @param offset how far from the cursor the lens sits.
/// @return the lens's top-left corner, in the owner's coordinates.
inline QPoint calculateQuickZoomLensPosition(const QPoint& cursorInOwner, const QSize& lensSize, const QSize& ownerSize,
                                             const QPoint& offset = QPoint(25, 25))
{
    QPoint lensPos = cursorInOwner + offset;

    if (lensPos.x() + lensSize.width() > ownerSize.width())
    {
        lensPos.setX(lensPos.x() - lensSize.width() - 50);
    }

    if (lensPos.y() + lensSize.height() > ownerSize.height())
    {
        lensPos.setY(lensPos.y() - lensSize.height() - 50);
    }

    return lensPos;
}

/// The region a lens magnifies around a widget position.
///
/// @param mousePos the cursor, in the widget's coordinates.
/// @param scaledImageOffset where the image's first pixel is drawn in the widget.
/// @param currentScale the display scale.
/// @param imageWidth the image's width.
/// @param imageHeight the image's height.
/// @param cropSize the region's side, at most the image's.
/// @return the region and the position under the cursor; an empty region for a scale or image that is not positive.
inline QuickZoomRegion calculateQuickZoomRegion(const QPoint& mousePos, const QPoint& scaledImageOffset,
                                                double currentScale, int imageWidth, int imageHeight, int cropSize = 24)
{
    QuickZoomRegion region;

    if (currentScale <= 0.0 || imageWidth <= 0 || imageHeight <= 0 || cropSize <= 0)
    {
        return region;
    }

    const double absolutePosX = static_cast<double>(mousePos.x() - scaledImageOffset.x()) * (1.0 / currentScale);
    const double absolutePosY = static_cast<double>(mousePos.y() - scaledImageOffset.y()) * (1.0 / currentScale);

    const int cropWidth = (std::min)(cropSize, imageWidth);
    const int cropHeight = (std::min)(cropSize, imageHeight);

    int x1 = static_cast<int>(absolutePosX - (cropWidth / 2.0));
    int y1 = static_cast<int>(absolutePosY - (cropHeight / 2.0));

    x1 = std::clamp(x1, 0, imageWidth - cropWidth);
    y1 = std::clamp(y1, 0, imageHeight - cropHeight);

    region.cropRect = QRect(x1, y1, cropWidth, cropHeight);
    region.absolutePosition = QPointF(absolutePosX, absolutePosY);

    return region;
}

} // namespace an::ui
