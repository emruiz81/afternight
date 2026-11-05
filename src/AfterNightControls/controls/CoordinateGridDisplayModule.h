// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The equatorial grid a view draws over a solved image: lines of constant right ascension and declination at a spacing
/// chosen from the visible field, projected through the image's solution, each labelled where it enters the view.
#pragma once

#include "AfterNightControlsExport.h"
#include "SexagesimalFormatter.h"
#include "controls/ImageViewControlListener.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>

#include <QPointF>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

class ImageViewControl;
class QPainter;

namespace an::ui
{

enum class CoordinateGridAxis : uint8_t
{
    RightAscension = 0,
    Declination
};

struct CoordinateGridProjectedLine
{
    CoordinateGridAxis axis = CoordinateGridAxis::RightAscension;
    double coordinateDegrees = 0.0;
    QVector<QPointF> points;
};

namespace detail
{

[[nodiscard]] inline double shortestAngularDistanceDegrees(const double from_deg, const double to_deg)
{
    double delta = an::normalizeRightAscensionDegrees(to_deg) - an::normalizeRightAscensionDegrees(from_deg);
    if (delta > 180.0)
    {
        delta -= 360.0;
    }
    else if (delta < -180.0)
    {
        delta += 360.0;
    }
    return delta;
}

[[nodiscard]] inline double chooseCoordinateGridSpacingDegrees(const double fov_width_deg)
{
    if (!std::isfinite(fov_width_deg) || fov_width_deg <= 0.0)
    {
        return 1.0 / 60.0;
    }

    if (fov_width_deg > 10.0)
    {
        return 2.0;
    }
    if (fov_width_deg > 2.0)
    {
        return 30.0 / 60.0;
    }
    if (fov_width_deg > 30.0 / 60.0)
    {
        return 5.0 / 60.0;
    }
    if (fov_width_deg > 5.0 / 60.0)
    {
        return 1.0 / 60.0;
    }
    return 10.0 / 3600.0;
}

/// A right ascension line's label: hours and minutes for a coarse grid, and seconds to a tenth for one of a minute or
/// finer.
[[nodiscard]] inline QString formatCoordinateGridRaLabel(const double ra_deg, const double spacing_deg)
{
    return spacing_deg <= (1.0 / 60.0) ? rightAscensionLabel(ra_deg, 1u) : rightAscensionMinutesLabel(ra_deg);
}

/// A declination line's label: degrees and arcminutes for a coarse grid, and arcseconds to a tenth for one of an
/// arcminute or finer.
[[nodiscard]] inline QString formatCoordinateGridDecLabel(const double dec_deg, const double spacing_deg)
{
    return spacing_deg <= (1.0 / 60.0) ? declinationLabel(dec_deg, 1u) : declinationMinutesLabel(dec_deg);
}

/// One grid line, sampled along its varying coordinate and projected through the solution into widget coordinates. A
/// sample the solution cannot place - behind the projection's horizon - is left out.
[[nodiscard]] inline CoordinateGridProjectedLine
projectCoordinateGridLine(const an::WcsSolution& solution, const CoordinateGridAxis axis,
                          const double fixed_coordinate_deg, const double range_start_deg, const double range_end_deg,
                          const double scale_factor, const int sample_count = 64)
{
    CoordinateGridProjectedLine line;
    line.axis = axis;
    line.coordinateDegrees = axis == CoordinateGridAxis::RightAscension
                                 ? an::normalizeRightAscensionDegrees(fixed_coordinate_deg)
                                 : fixed_coordinate_deg;

    if (!std::isfinite(scale_factor) || scale_factor <= 0.0)
    {
        return line;
    }

    const int clamped_samples = (std::max)(2, sample_count);
    const double span = range_end_deg - range_start_deg;

    for (int i = 0; i <= clamped_samples; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(clamped_samples);
        const double varying_coordinate = range_start_deg + (span * t);
        const double ra_deg = axis == CoordinateGridAxis::RightAscension
                                  ? an::normalizeRightAscensionDegrees(fixed_coordinate_deg)
                                  : an::normalizeRightAscensionDegrees(varying_coordinate);
        const double dec_deg = axis == CoordinateGridAxis::RightAscension ? varying_coordinate : fixed_coordinate_deg;

        if (!std::isfinite(dec_deg) || dec_deg < -90.0 || dec_deg > 90.0)
        {
            continue;
        }

        const an::Result<an::PixelCoordinate> pixel =
            solution.worldToPixel(an::SkyCoordinate{.rightAscensionDegrees = ra_deg, .declinationDegrees = dec_deg});
        if (!pixel.hasValue() || !std::isfinite(pixel->x) || !std::isfinite(pixel->y))
        {
            continue;
        }

        line.points.append(QPointF(pixel->x * scale_factor, pixel->y * scale_factor));
    }

    return line;
}

} // namespace detail

class AFTERNIGHTCONTROLS_API CoordinateGridDisplayModule final : public ImageViewControlListener,
                                                                 public ImageViewWidgetDisplayModule
{
public:
    explicit CoordinateGridDisplayModule(ImageViewControl* control);
    ~CoordinateGridDisplayModule() override;

    void onDraw(QPainter& painter) override;

    void onImageLoaded(ImageViewControl* ctrl) override;
    void onImageUpdated(ImageViewControl* ctrl) override;
    void onImageRenamed(ImageViewControl* ctrl) override;
    void onImageViewControlDestroyed(ImageViewControl* ctrl) override;

    /// How many lines the last projection kept.
    [[nodiscard]] int projectedLineCount() const;

private:
    void invalidateCache();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
