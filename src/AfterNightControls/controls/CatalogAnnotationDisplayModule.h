// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The catalogue annotations a view draws over a solved image: the bundled catalogues' deep-sky objects and bright
/// stars, each a layer the view draws or not, kept, merged and named as an annotated picture keeps them at the level of
/// detail the view's choices name, and laid out together as it lays out its labels - no label covers another - with
/// every label the same size on the screen at any zoom, saying what the choices say, in the palette they name.
///
/// **Nothing waits on the GUI thread.** Each catalogue is read and queried for the whole image's field on a worker
/// (`detail::FieldCatalogQueries`), which builds its layer's scene at the level; the layers' scenes are laid out at the
/// view's zoom on a worker whenever the zoom, the labels or what is drawn changes (`detail::layoutOverlayAnnotations`),
/// so a scroll draws the layout there is, and a zoom draws the last one until its own lands. The level's smallest size
/// is a share of the part of the image the view shows, so zooming in names the smaller objects. A paint draws the
/// layout through the annotated picture's renderer over the part of the image the view shows.
#pragma once

#include "AfterNightControlsExport.h"
#include "app/BundledCatalogs.h"
#include "controls/ImageViewControlListener.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>

#include <QPointF>
#include <QRectF>
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

struct AnnotationViewChoices;

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

    /// Sets what the overlay names and how it looks, as the view's menu chose: the level of detail it names the
    /// catalogues at, what a label says and the palette it draws in. Until the new labels land, the last ones are
    /// drawn.
    ///
    /// @param choices the choices.
    void setChoices(const AnnotationViewChoices& choices);

    /// What the overlay names and how it looks.
    [[nodiscard]] AnnotationViewChoices choices() const;

    /// Which catalogues the overlay draws: the deep-sky objects, the bright stars, or both - as it does unless told
    /// otherwise. A catalogue not drawn is not asked.
    ///
    /// @param deepSky whether the deep-sky objects are drawn.
    /// @param stars whether the bright stars are drawn.
    void setLayers(bool deepSky, bool stars);

    /// Whether the overlay draws the deep-sky objects.
    [[nodiscard]] bool showsDeepSky() const;

    /// Whether the overlay draws the bright stars.
    [[nodiscard]] bool showsStars() const;

    /// The first line of each label the layout drawn last placed, in the order it placed them: the most prominent
    /// object's first.
    [[nodiscard]] QVector<QString> projectedLabels() const;

    /// Where those labels were drawn, in the widget's pixels at the scale they were drawn at.
    [[nodiscard]] QVector<QRectF> projectedLabelBoxes() const;

    /// How many catalog queries the overlay asked a worker for, of either catalogue.
    [[nodiscard]] std::uint64_t catalogQueriesAsked() const;

    /// How many layouts made on workers landed to be drawn.
    [[nodiscard]] std::uint64_t layoutsMade() const;

    /// Reads the catalogues' text from another source than the product's resources.
    void setCatalogSource(BundledCatalogSource source);

private:
    /// Sets aside the layout drawn: what it was made of changed.
    void invalidate();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
