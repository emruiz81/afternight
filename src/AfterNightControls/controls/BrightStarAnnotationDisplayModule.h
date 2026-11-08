// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The bright-star labels a view draws over a solved image: the bundled catalog's stars inside the visible field, each
/// marked and labelled with its proper name, its Bayer letter or its designation.
///
/// The catalog is read and queried for the whole image's field on a worker (`detail::FieldCatalogQueries`), which
/// keeps, orders and names its stars as an annotated picture does (`detail::overlayAnnotationContent`); a paint draws
/// those near what the view shows at its scale.
#pragma once

#include "AfterNightControlsExport.h"
#include "app/BundledCatalogs.h"
#include "controls/ImageViewControlListener.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <QPointF>
#include <QString>
#include <QVector>

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
