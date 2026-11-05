// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The SIP distortion map a view draws over a solved image: a grid of arrows, each from a pixel to where the solution's
/// forward polynomial carries it, scaled so the largest is visible and coloured by its length.
#pragma once

#include "AfterNightControlsExport.h"
#include "controls/ImageViewControlListener.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>

#include <QPointF>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

class ImageViewControl;
class QPainter;

namespace an::ui
{

/// One arrow of the map: a pixel, in engine pixel coordinates, and where the forward polynomial carries it.
struct DistortionVectorSample
{
    QPointF sourceImagePoint;
    QPointF correctedImagePoint;
    double magnitudePixels = 0.0;

    [[nodiscard]] QPointF displacement() const
    {
        return correctedImagePoint - sourceImagePoint;
    }
};

namespace detail
{

/// The arrows of a grid of pixel centres spread evenly from the first pixel's centre to the last's, each the forward
/// polynomial's correction there. A model with no forward polynomial draws none.
[[nodiscard]] inline QVector<DistortionVectorSample>
sampleDistortionVectors(const an::SipDistortion& distortion, const int image_width, const int image_height,
                        const int grid_columns = 20, const int grid_rows = 20)
{
    QVector<DistortionVectorSample> samples;
    if (!distortion.hasForwardModel() || image_width <= 0 || image_height <= 0)
    {
        return samples;
    }

    const int clamped_columns = (std::max)(2, grid_columns);
    const int clamped_rows = (std::max)(2, grid_rows);
    // Pixel centres sit at one half past each whole pixel, so the grid spans the first centre to the last.
    const double span_x = static_cast<double>((std::max)(0, image_width - 1));
    const double span_y = static_cast<double>((std::max)(0, image_height - 1));
    samples.reserve(clamped_columns * clamped_rows);

    for (int row = 0; row < clamped_rows; ++row)
    {
        const double y = 0.5 + (span_y * static_cast<double>(row)) / static_cast<double>(clamped_rows - 1);

        for (int column = 0; column < clamped_columns; ++column)
        {
            const double x = 0.5 + (span_x * static_cast<double>(column)) / static_cast<double>(clamped_columns - 1);

            const an::Result<an::PixelCoordinate> corrected =
                an::applySipForward(distortion, an::PixelCoordinate{.x = x, .y = y});
            if (!corrected.hasValue() || !std::isfinite(corrected->x) || !std::isfinite(corrected->y))
            {
                continue;
            }

            DistortionVectorSample sample;
            sample.sourceImagePoint = QPointF(x, y);
            sample.correctedImagePoint = QPointF(corrected->x, corrected->y);
            sample.magnitudePixels = std::hypot(corrected->x - x, corrected->y - y);
            samples.append(sample);
        }
    }

    return samples;
}

} // namespace detail

class AFTERNIGHTCONTROLS_API DistortionMapDisplayModule final : public ImageViewControlListener,
                                                                public ImageViewWidgetDisplayModule
{
public:
    explicit DistortionMapDisplayModule(ImageViewControl* control);
    ~DistortionMapDisplayModule() override;

    void onDraw(QPainter& painter) override;

    void onImageLoaded(ImageViewControl* ctrl) override;
    void onImageUpdated(ImageViewControl* ctrl) override;
    void onImageRenamed(ImageViewControl* ctrl) override;
    void onImageViewControlDestroyed(ImageViewControl* ctrl) override;

    /// How many arrows the last sampling kept.
    [[nodiscard]] int sampledVectorCount() const;

private:
    void invalidateCache();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
