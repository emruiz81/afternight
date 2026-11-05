// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The astrometry overlays as the image view reaches them through its companion seam
/// (`controls/ImageViewCompanions.h`): the detected stars, the equatorial grid, the distortion map, the deep-sky
/// annotations and the bright-star labels, made here, beside the overlays themselves.

#include "stdafx.hpp"

#include "controls/AstrometryStarDisplayModule.h"
#include "controls/BrightStarAnnotationDisplayModule.h"
#include "controls/CatalogAnnotationDisplayModule.h"
#include "controls/CoordinateGridDisplayModule.h"
#include "controls/DistortionMapDisplayModule.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"

#include <utility>

namespace an::ui
{

std::unique_ptr<::ImageViewWidgetDisplayModule> createAstrometryOverlay(const AstrometryOverlay overlay,
                                                                        ::ImageViewControl& view)
{
    switch (overlay)
    {
    case AstrometryOverlay::DetectedStars:
        return std::make_unique<AstrometryStarDisplayModule>();
    case AstrometryOverlay::CoordinateGrid:
        return std::make_unique<CoordinateGridDisplayModule>(&view);
    case AstrometryOverlay::DistortionMap:
        return std::make_unique<DistortionMapDisplayModule>(&view);
    case AstrometryOverlay::CatalogAnnotations:
        return std::make_unique<CatalogAnnotationDisplayModule>(&view);
    case AstrometryOverlay::BrightStarLabels:
        return std::make_unique<BrightStarAnnotationDisplayModule>(&view);
    }
    return nullptr;
}

void setAstrometryOverlayMagnitudeLimit(::ImageViewWidgetDisplayModule& overlay, const double limit)
{
    if (auto* annotations = dynamic_cast<CatalogAnnotationDisplayModule*>(&overlay); annotations != nullptr)
    {
        annotations->setMagnitudeLimit(limit);
        return;
    }
    if (auto* labels = dynamic_cast<BrightStarAnnotationDisplayModule*>(&overlay); labels != nullptr)
    {
        labels->setMagnitudeLimit(limit);
    }
}

void setAstrometryOverlayStars(::ImageViewWidgetDisplayModule& overlay, std::shared_ptr<const ViewOverlayStars> stars)
{
    if (auto* detected = dynamic_cast<AstrometryStarDisplayModule*>(&overlay); detected != nullptr)
    {
        detected->setStars(std::move(stars));
    }
}

} // namespace an::ui
