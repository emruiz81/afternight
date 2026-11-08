// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The astrometry overlays as the image view reaches them through its companion seam
/// (`controls/ImageViewCompanions.h`): the detected stars, the equatorial grid, the distortion map and the catalogue
/// annotations - the deep-sky objects and the bright stars - made here, beside the overlays themselves.

#include "stdafx.hpp"

#include "controls/AstrometryStarDisplayModule.h"
#include "controls/CatalogAnnotationDisplayModule.h"
#include "controls/CoordinateGridDisplayModule.h"
#include "controls/DistortionMapDisplayModule.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/annotation/AnnotationSettings.h"

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
    }
    return nullptr;
}

void setAstrometryOverlayChoices(::ImageViewWidgetDisplayModule& overlay, const AnnotationViewChoices& choices)
{
    if (auto* annotations = dynamic_cast<CatalogAnnotationDisplayModule*>(&overlay); annotations != nullptr)
    {
        annotations->setChoices(choices);
    }
    else if (auto* grid = dynamic_cast<CoordinateGridDisplayModule*>(&overlay); grid != nullptr)
    {
        grid->setPalette(choices.palette);
    }
}

void setAstrometryOverlayLayers(::ImageViewWidgetDisplayModule& overlay, const bool deepSky, const bool stars)
{
    if (auto* annotations = dynamic_cast<CatalogAnnotationDisplayModule*>(&overlay); annotations != nullptr)
    {
        annotations->setLayers(deepSky, stars);
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
