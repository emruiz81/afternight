// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The deep-sky annotations a view draws over a solved image: the bundled catalog's objects inside the visible field,
/// each outlined by its size and position angle and labelled with its designation.
///
/// The catalog is read and queried for the whole image's field on a worker (`detail::FieldCatalogQueries`), which
/// keeps, merges and names its objects as an annotated picture does (`detail::overlayAnnotationContent`), so the view
/// and a picture of it name the same objects alike; a paint draws those near what the view shows at its scale.
#pragma once

#include "AfterNightControlsExport.h"
#include "app/BundledCatalogs.h"
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
#include <numbers>

class ImageViewControl;
class QPainter;

namespace an::ui
{

struct CatalogAnnotationProjectedShape
{
    QPointF center;
    QVector<QPointF> outline;

    [[nodiscard]] bool is_valid() const
    {
        return !outline.isEmpty() || !center.isNull();
    }
};

namespace detail
{

/// Where an object is drawn: its centre, and its outline when the catalog states its size - an ellipse of its major
/// and minor axes at its position angle east of north, sampled and projected through the solution.
[[nodiscard]] inline CatalogAnnotationProjectedShape projectCatalogObjectAnnotation(const an::WcsSolution& solution,
                                                                                    const an::CatalogEntry& object,
                                                                                    const double scale_factor,
                                                                                    const int sample_count = 48)
{
    CatalogAnnotationProjectedShape shape;
    if (!std::isfinite(scale_factor) || scale_factor <= 0.0)
    {
        return shape;
    }

    const an::Result<an::PixelCoordinate> center = solution.worldToPixel(object.coordinate);
    if (!center.hasValue())
    {
        return shape;
    }

    shape.center = QPointF(center->x * scale_factor, center->y * scale_factor);
    if (!object.extent.has_value() ||
        (object.extent->majorAxisArcminutes <= 0.0 && object.extent->minorAxisArcminutes <= 0.0))
    {
        return shape;
    }

    const an::CatalogExtent& extent = *object.extent;
    const double semi_major_deg = (std::max)(0.0, extent.majorAxisArcminutes) / 120.0;
    const double semi_minor_deg =
        (std::max)(0.0, extent.minorAxisArcminutes > 0.0 ? extent.minorAxisArcminutes : extent.majorAxisArcminutes) /
        120.0;
    if (semi_major_deg <= 0.0 || semi_minor_deg <= 0.0)
    {
        return shape;
    }

    const double ra_deg = object.coordinate.rightAscensionDegrees;
    const double dec_deg = object.coordinate.declinationDegrees;
    const double pa_rad = extent.positionAngleDegrees * std::numbers::pi / 180.0;
    const double cos_dec =
        (std::max)(1.0e-6, std::cos(std::clamp(dec_deg, -89.999, 89.999) * std::numbers::pi / 180.0));
    const int clamped_samples = (std::max)(16, sample_count);
    for (int i = 0; i < clamped_samples; ++i)
    {
        const double theta = (static_cast<double>(i) / static_cast<double>(clamped_samples)) * (2.0 * std::numbers::pi);
        const double u = semi_major_deg * std::cos(theta);
        const double v = semi_minor_deg * std::sin(theta);
        const double east_deg = (u * std::sin(pa_rad)) + (v * std::cos(pa_rad));
        const double north_deg = (u * std::cos(pa_rad)) - (v * std::sin(pa_rad));

        const an::SkyCoordinate point{.rightAscensionDegrees =
                                          an::normalizeRightAscensionDegrees(ra_deg + (east_deg / cos_dec)),
                                      .declinationDegrees = std::clamp(dec_deg + north_deg, -90.0, 90.0)};
        const an::Result<an::PixelCoordinate> outline = solution.worldToPixel(point);
        if (!outline.hasValue() || !std::isfinite(outline->x) || !std::isfinite(outline->y))
        {
            continue;
        }

        shape.outline.append(QPointF(outline->x * scale_factor, outline->y * scale_factor));
    }

    return shape;
}

} // namespace detail

class AFTERNIGHTCONTROLS_API CatalogAnnotationDisplayModule final : public ImageViewControlListener,
                                                                    public ImageViewWidgetDisplayModule
{
public:
    explicit CatalogAnnotationDisplayModule(ImageViewControl* control);
    ~CatalogAnnotationDisplayModule() override;

    void onDraw(QPainter& painter) override;

    void onImageLoaded(ImageViewControl* ctrl) override;
    void onImageUpdated(ImageViewControl* ctrl) override;
    void onImageRenamed(ImageViewControl* ctrl) override;
    void onImageViewControlDestroyed(ImageViewControl* ctrl) override;

    void setMagnitudeLimit(double magnitude_limit);
    [[nodiscard]] double magnitudeLimit() const;

    /// The labels the last projection drew, in drawing order.
    [[nodiscard]] QVector<QString> projectedLabels() const;

    /// How many catalog queries the overlay asked a worker for.
    [[nodiscard]] std::uint64_t catalogQueriesAsked() const;

    /// Reads the catalog's text from another source than the product's resources.
    void setCatalogSource(BundledCatalogSource source);

private:
    void invalidateCache();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
