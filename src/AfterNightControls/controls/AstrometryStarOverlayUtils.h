// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where the detected-stars overlay draws a star: an ellipse of its fitted full widths at its fitted angle, around its
/// position. The position is an engine pixel coordinate, so it scales straight onto the view's widget.
#pragma once

#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>

#include <QPointF>

#include <algorithm>
#include <cmath>

namespace an::ui::detail
{

struct AstrometryStarOverlayEllipse
{
    QPointF center;
    double semiMajorRadius = 0.0;
    double axisRatio = 1.0;
    double rotationDegrees = 0.0;

    [[nodiscard]] bool isValid() const
    {
        return std::isfinite(center.x()) && std::isfinite(center.y()) && std::isfinite(semiMajorRadius) &&
               semiMajorRadius > 0.0 && std::isfinite(axisRatio) && axisRatio > 0.0 && std::isfinite(rotationDegrees);
    }
};

/// The ellipse a star is drawn as: twice its major full width as the radius, its minor width over its major as the
/// axis ratio, a unit width standing in for a width that is not positive.
[[nodiscard]] inline AstrometryStarOverlayEllipse projectAstrometryStarOverlayEllipse(const an::DetectedStar& star,
                                                                                      const double scale_factor)
{
    AstrometryStarOverlayEllipse ellipse;
    if (!std::isfinite(scale_factor) || scale_factor <= 0.0)
    {
        return ellipse;
    }

    const double base_fwhm_x = star.psf.fullWidthX > 0.0 ? star.psf.fullWidthX : 1.0;
    const double base_fwhm_y = star.psf.fullWidthY > 0.0 ? star.psf.fullWidthY : base_fwhm_x;

    ellipse.center = QPointF(star.positionX * scale_factor, star.positionY * scale_factor);
    ellipse.semiMajorRadius = (std::max)(1.0, base_fwhm_x * 2.0 * scale_factor);
    ellipse.axisRatio = (std::max)(0.05, base_fwhm_y / (std::max)(base_fwhm_x, 1.0e-6));
    ellipse.rotationDegrees = 90.0 - star.psf.angleDegrees;
    return ellipse;
}

} // namespace an::ui::detail
