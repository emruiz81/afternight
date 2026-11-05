// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QImage>

#include "AfterNightControlsExport.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

namespace an::ui
{

class AFTERNIGHTCONTROLS_API ComparisonDisplayModule : public ImageViewWidgetDisplayModule
{
public:
    // Sets the overlay image to blend on top of the base image during onDraw().
    // The image must be the full-resolution source image, not a pre-scaled tile.
    // Qt scales it into getCurrentScaledImageArea() at paint time, so passing a
    // down-sampled copy will produce a blurry overlay.
    void setOverlayImage(const QImage& image);
    void clearOverlayImage();
    [[nodiscard]] bool hasOverlayImage() const;

    void setOverlayOpacity(double opacity);
    [[nodiscard]] double overlayOpacity() const;

    void onDraw(QPainter& painter) override;

private:
    void requestUpdate();

    QImage m_overlayImage;
    double m_overlayOpacity = 0.5;
};

} // namespace an::ui
