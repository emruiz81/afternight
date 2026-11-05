// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The bright-star labels a view draws over a solved image: the bundled catalog's stars inside the visible field, each
/// marked and labelled with its proper name, its Bayer letter or its designation.
///
/// The catalog is read and queried for the whole image's field on a worker (`detail::FieldCatalogQueries`), and a paint
/// projects the stars near what the view shows through the image's solution.
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

#include <cmath>
#include <cstdint>
#include <memory>

class ImageViewControl;
class QPainter;

namespace an::ui
{

struct BrightStarAnnotationProjectedPoint
{
    QPointF center;
    bool valid = false;

    [[nodiscard]] bool isValid() const
    {
        return valid;
    }
};

namespace detail
{

/// Where a star is drawn, through the solution.
[[nodiscard]] inline BrightStarAnnotationProjectedPoint
projectBrightStarAnnotation(const an::WcsSolution& solution, const an::CatalogEntry& star, const double scale_factor)
{
    BrightStarAnnotationProjectedPoint projected;
    if (!std::isfinite(scale_factor) || scale_factor <= 0.0)
    {
        return projected;
    }

    const an::Result<an::PixelCoordinate> pixel = solution.worldToPixel(star.coordinate);
    if (!pixel.hasValue() || !std::isfinite(pixel->x) || !std::isfinite(pixel->y))
    {
        return projected;
    }

    projected.center = QPointF(pixel->x * scale_factor, pixel->y * scale_factor);
    projected.valid = true;
    return projected;
}

/// A star's label: its proper name, else its Bayer letter, else its designation.
[[nodiscard]] inline QString formatBrightStarAnnotationLabel(const an::CatalogEntry& star)
{
    const QString common_name = QString::fromStdString(star.commonName).trimmed();
    if (!common_name.isEmpty())
    {
        return common_name;
    }

    const QString bayer = QString::fromStdString(star.designation).trimmed();
    if (!bayer.isEmpty())
    {
        return bayer;
    }

    return QString::fromStdString(star.identifier).trimmed();
}

} // namespace detail

class AFTERNIGHTCONTROLS_API BrightStarAnnotationDisplayModule final : public ImageViewControlListener,
                                                                       public ImageViewWidgetDisplayModule
{
public:
    explicit BrightStarAnnotationDisplayModule(ImageViewControl* control);
    ~BrightStarAnnotationDisplayModule() override;

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
