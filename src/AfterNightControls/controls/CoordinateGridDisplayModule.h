// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The equatorial grid a view draws over a solved image: an annotated picture's grid of the part the view shows - its
/// lines of constant right ascension and declination at the round steps that part's own sky calls for, labelled where
/// they meet the part's edges (`detail::layoutOverlayGrid`) - drawn through the picture's renderer in its palette,
/// every label the same size on the screen at any zoom. The grid follows the view: a scroll or a zoom lays it out
/// again.
#pragma once

#include "AfterNightControlsExport.h"
#include "controls/ImageViewControlListener.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <QRectF>
#include <QString>
#include <QVector>

#include <memory>

class ImageViewControl;
class QPainter;

namespace an::ui
{

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

    /// How many lines the last layout of the part shown kept.
    [[nodiscard]] int projectedLineCount() const;

    /// The labels the last layout placed, in the order it placed them.
    [[nodiscard]] QVector<QString> projectedLabels() const;

    /// Where those labels were drawn, in the widget's pixels.
    [[nodiscard]] QVector<QRectF> projectedLabelBoxes() const;

private:
    /// Sets aside the grid laid out: what it was made of changed.
    void invalidate();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
